#include "hsm_worker.h"
#include "../common/logger.h"
#include "../config/config_manager.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <openssl/evp.h>

using namespace std;

// Helper function to convert a single hex character to its integer value.
static uint8_t hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    throw std::invalid_argument("Invalid hex character");
}

// Robustly converts a hex string to a vector of bytes.
static vector<uint8_t> safe_hex_to_bytes(const string& hex) {
    vector<uint8_t> bytes;
    if (hex.length() % 2 != 0) {
        throw std::invalid_argument("Hex string must have an even number of characters.");
    }
    bytes.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        uint8_t hi = hex_char_to_val(hex[i]);
        uint8_t lo = hex_char_to_val(hex[i+1]);
        bytes.push_back((hi << 4) | lo);
    }
    return bytes;
}

// Converts a string of digits to BCD (Binary-Coded Decimal).
static std::vector<uint8_t> string_to_bcd(const std::string& str) {
    std::vector<uint8_t> bcd;
    std::string s = str;
    if (s.length() % 2 != 0) {
        s = "0" + s; // Pad with a leading zero for odd-length strings
    }
    bcd.reserve(s.length() / 2);
    for (size_t i = 0; i < s.length(); i += 2) {
        uint8_t byte = 0;
        if (s[i] >= '0' && s[i] <= '9') {
            byte |= (s[i] - '0') << 4;
        }
        if (s[i+1] >= '0' && s[i+1] <= '9') {
            byte |= (s[i+1] - '0');
        }
        bcd.push_back(byte);
    }
    return bcd;
}

// Calcula o KCV de uma chave usando DES-EDE-ECB (Triple-DES, Double-Length).
// Cifra 8 bytes zerados com a chave e retorna os primeiros 3 bytes do resultado.
// Este é o algoritmo padrão do mercado financeiro (ISO 11568-2).
static std::vector<uint8_t> calculate_kcv_tdes(const std::vector<uint8_t>& key) {
    const uint8_t zeros[8] = {0};
    std::vector<uint8_t> result(8, 0);
    int out_len = 0;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Falha ao criar contexto EVP para cálculo de KCV.");
    }

    // Seleciona o cipher correto conforme o tamanho da chave
    const EVP_CIPHER* cipher = (key.size() == 16) ? EVP_des_ede_ecb() : EVP_des_ede3_ecb();

    EVP_EncryptInit_ex(ctx, cipher, nullptr, key.data(), nullptr);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_EncryptUpdate(ctx, result.data(), &out_len, zeros, 8);
    EVP_CIPHER_CTX_free(ctx);

    // Retorna apenas os 3 primeiros bytes (KCV padrão)
    result.resize(3);
    return result;
}

HsmWorker& HsmWorker::get_instance() {
    static HsmWorker instance;
    return instance;
}

HsmWorker::HsmWorker() {}

HsmWorker::~HsmWorker() {
    if (is_running_) {
        worker_thread_.request_stop();
        cv_.notify_all();
        if (io_context_) io_context_->stop();
        if (io_thread_.joinable()) io_thread_.join();
    }
}

void HsmWorker::start(const string& hsm_ip, short hsm_port) {
    if (is_running_) return;

    const auto& config = ConfigManager::getInstance().getConfig();
    hsm_ip_ = config.value("/hsm/ip"_json_pointer, hsm_ip);
    hsm_port_ = config.value("/hsm/port"_json_pointer, hsm_port);
    source_pek_kcv_ = config.value("/hsm/source_pek_kcv"_json_pointer, "A1B2C3");
    source_pek_index_ = config.value("/hsm/source_pek_index"_json_pointer, 0);
    destination_pek_index_ = config.value("/hsm/destination_pek_index"_json_pointer, 1);
    zpk_hex_ = config.value("/hsm/zpk_hex"_json_pointer, "");

    is_running_ = true;

    io_context_ = make_unique<asio::io_context>();
    socket_ = make_unique<asio::ip::tcp::socket>(*io_context_);
    reconnect_timer_ = make_unique<asio::steady_timer>(*io_context_);

    connect_hsm();

    io_thread_ = thread([this]() {
        asio::io_context::work idle_work(*io_context_);
        io_context_->run();
    });

    worker_thread_ = jthread([this](stop_token stoken) {
        this->worker_loop(stoken);
    });

    LOG_INFO("Thread do Worker HSM iniciada. Conectando ao SoftHSM em {}:{}", hsm_ip_, hsm_port_);
}

void HsmWorker::connect_hsm() {
    if (socket_->is_open()) {
        asio::error_code ec;
        socket_->close(ec);
    }

    asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(hsm_ip_), hsm_port_);
    socket_->async_connect(endpoint, [this](asio::error_code ec) {
        if (!ec) {
            hsm_connected_ = true;
            LOG_INFO("Worker HSM: Conectado com sucesso ao SoftHSM.");
            do_read_header();
        } else {
            LOG_ERROR("Worker HSM: Falha ao conectar ao HSM ({}). Tentando novamente em 3 segundos...", ec.message());
            schedule_reconnect();
        }
    });
}

void HsmWorker::schedule_reconnect() {
    hsm_connected_ = false;
    reconnect_timer_->expires_after(chrono::seconds(3));
    reconnect_timer_->async_wait([this](asio::error_code ec) {
        if (!ec && is_running_) {
            LOG_INFO("Worker HSM: Tentando reconectar...");
            connect_hsm();
        }
    });
}

void HsmWorker::enqueue_job(unique_ptr<IsoMessage> msg, HsmResultCallback callback) {
    {
        lock_guard<mutex> lock(queue_mutex_);
        job_queue_.push(HsmJob{move(msg), move(callback)});
    }
    cv_.notify_one();
}

std::string HsmWorker::translate_pin_sync(const std::string& pin_block, const std::string& pan) {
    if (!hsm_connected_) {
        LOG_ERROR("HSM não está conectado. Não é possível traduzir o PIN.");
        return "";
    }

    // Validate input parameters
    if (pin_block.empty()) {
        LOG_ERROR("Bloco de PIN está vazio. Tradução cancelada.");
        return "";
    }
    if (pin_block.length() != 16) {
        LOG_ERROR("Bloco de PIN tem tamanho inválido: {} (esperado 16 caracteres hexadecimais).", pin_block.length());
        return "";
    }
    if (pan.empty()) {
        LOG_ERROR("PAN está vazio. Tradução cancelada.");
        return "";
    }
    if (pan.length() < 13) {
        LOG_ERROR("PAN tem tamanho inválido: {} (mínimo 13 dígitos requerido).", pan.length());
        return "";
    }

    auto msg = std::make_unique<IsoMessage>();
    msg->set_field(2, pan);
    msg->set_field(52, pin_block);

    std::promise<std::string> pin_promise;
    std::future<std::string> pin_future = pin_promise.get_future();

    enqueue_job(std::move(msg), [&pin_promise](std::unique_ptr<IsoMessage> result_msg) {
        if (result_msg && result_msg->has_field(52)) {
            pin_promise.set_value(result_msg->get_field(52));
        } else {
            pin_promise.set_value("");
        }
    });

    std::future_status status = pin_future.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::ready) {
        return pin_future.get();
    } else {
         LOG_ERROR("Worker HSM: Timeout esperando pela tradução do PIN (5 segundos).");
         return "";
    }
}

void HsmWorker::worker_loop(stop_token stoken) {
    while (!stoken.stop_requested()) {
        HsmJob job;
        {
            unique_lock<mutex> lock(queue_mutex_);
            cv_.wait(lock, stoken, [this]() { return !job_queue_.empty(); });
            if (stoken.stop_requested()) break;
            job = move(job_queue_.front());
            job_queue_.pop();
        }

        if (hsm_connected_) {
            perform_pin_translation(move(job));
        } else {
            LOG_ERROR("Worker HSM: HSM está DESCONECTADO. Negando transação.");
            if (job.on_complete) job.on_complete(nullptr);
        }
    }
}

void HsmWorker::perform_pin_translation(HsmJob job) {
    uint16_t seq_id = current_sequence_id_++;
    auto pending_req = std::make_shared<HsmPendingRequest>();
    pending_req->original_msg = std::shared_ptr<IsoMessage>(new IsoMessage(*job.msg));
    pending_req->on_complete = job.on_complete;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_[seq_id] = pending_req;
    }

    try {
        if (!job.msg->has_field(52) || !job.msg->has_field(2)) {
            throw std::runtime_error("Faltando PAN (Campo 2) ou Bloco de PIN (Campo 52).");
        }

        std::string pan_str = job.msg->get_field(2);
        std::string pin_block_hex = job.msg->get_field(52);

        // Validate PAN format
        if (pan_str.empty()) {
            throw std::runtime_error("PAN não pode estar vazio.");
        }
        if (pan_str.length() < 13) {
            throw std::runtime_error("PAN deve conter no mínimo 13 dígitos.");
        }
        if (pan_str.find_first_not_of("0123456789") != std::string::npos) {
            throw std::runtime_error("PAN deve conter apenas dígitos.");
        }

        // Validate PIN block format
        if (pin_block_hex.length() != 16) {
            throw std::runtime_error("Tamanho inválido do Bloco de PIN. Deve ter 16 caracteres hexadecimais.");
        }
        if (pin_block_hex.find_first_not_of("0123456789ABCDEFabcdef") != std::string::npos) {
            throw std::runtime_error("Bloco de PIN contém caracteres hexadecimais inválidos.");
        }

        // --- Bug #1 Fix: Calcula KCV dinamicamente via 3DES-ECB (DES-EDE) ---
        // O KCV correto é obtido cifrando 8 bytes zero com a ZPK usando DES-EDE-ECB.
        // NÃO usar AES, NÃO usar o valor estático do config.json.
        const auto zpk_bytes = safe_hex_to_bytes(zpk_hex_);
        if (zpk_bytes.size() != 16 && zpk_bytes.size() != 24) {
            throw std::runtime_error("ZPK inválida: deve ter 16 ou 24 bytes.");
        }
        const auto kcv_bytes = calculate_kcv_tdes(zpk_bytes);

        // Formata KCV calculado para log
        std::stringstream kcv_ss;
        for (uint8_t b : kcv_bytes) kcv_ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)b;
        const std::string kcv_hex_log = kcv_ss.str();

        LOG_DEBUG("Worker HSM: KCV calculado via 3DES-ECB: {} (config ignorado: '{}')", kcv_hex_log, source_pek_kcv_);
        LOG_DEBUG("Worker HSM: Preparando pacote 'CT'. Seq ID: {}, PAN: {}, Bloco de PIN: {}, KCV: {}", seq_id, pan_str, pin_block_hex, kcv_hex_log);

        // --- Bug #2 Fix: Serialização correta do pacote CT (protocolo exato = 37 bytes) ---
        // Estrutura: [0-1] Length | [2-3] "CT" | [4-5] SeqID | [6] InFmt | [7-22] PINBlock(16) |
        //            [23] OutFmt | [24-25] ZPKIdx(2) | [26-33] PAN(8 bytes RAW) | [34-36] KCV(3 bytes RAW)

        // PIN Block: a string hex de 16 chars → 8 bytes RAW
        const auto pin_bytes = safe_hex_to_bytes(pin_block_hex);  // 16 hex chars → 8 bytes
        if (pin_bytes.size() != 8) {
            throw std::runtime_error("PIN Block deve ter 8 bytes RAW (16 chars hex).");
        }

        // PAN: extrair 12 dígitos do meio, prefixar com "0000", converter para 8 bytes BCD
        std::string pan_12_digits;
        if (pan_str.length() > 12) {
            pan_12_digits = pan_str.substr(pan_str.length() - 13, 12);
        } else {
            std::stringstream ss;
            ss << std::setfill('0') << std::setw(12) << pan_str;
            pan_12_digits = ss.str();
        }
        const std::string formatted_pan = "0000" + pan_12_digits;   // 16 hex chars
        const auto pan_bytes = string_to_bcd(formatted_pan);        // → 8 bytes RAW
        if (pan_bytes.size() != 8) {
            throw std::runtime_error("PAN formatado deve resultar em 8 bytes BCD.");
        }

        LOG_DEBUG("Worker HSM: Seq ID: {}. PAN (12 dígitos): {}. PAN BCD (8 bytes): {}", seq_id, pan_12_digits, formatted_pan);

        // Monta payload seguindo a estrutura exata do protocolo
        std::vector<uint8_t> payload;
        payload.reserve(35);  // 35 bytes de payload (sem o length prefix)

        // [0-1] Command Code: "CT"
        payload.push_back('C');
        payload.push_back('T');

        // [2-3] Sequence ID (uint16, Big-Endian)
        payload.push_back((seq_id >> 8) & 0xFF);
        payload.push_back(seq_id & 0xFF);

        // [4] In Format (ISO-4 = 0x06)
        payload.push_back(0x06);

        // [5-20] PIN Block In (16 bytes RAW)
        payload.insert(payload.end(), pin_bytes.begin(), pin_bytes.end());

        // [21] Out Format (ISO-3 = 0x02)
        payload.push_back(0x02);

        // [22-23] ZPK Index (destination_pek_index_, uint16, Big-Endian)
        payload.push_back((destination_pek_index_ >> 8) & 0xFF);
        payload.push_back(destination_pek_index_ & 0xFF);

        // [24-31] PAN Masked (8 bytes RAW, BCD)
        payload.insert(payload.end(), pan_bytes.begin(), pan_bytes.end());

        // [32-34] KCV (3 bytes RAW, calculado via 3DES-ECB)
        payload.insert(payload.end(), kcv_bytes.begin(), kcv_bytes.end());

        // Prefixo de tamanho (2 bytes, Big-Endian) — NÃO faz parte do payload contado
        std::vector<uint8_t> packet;
        uint16_t length = static_cast<uint16_t>(payload.size());
        packet.push_back((length >> 8) & 0xFF);
        packet.push_back(length & 0xFF);
        packet.insert(packet.end(), payload.begin(), payload.end());

        // Loga o frame completo e valida tamanho esperado (37 bytes = 2 length + 35 payload)
        std::stringstream ss;
        for (uint8_t b : packet) ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)b;
        LOG_DEBUG("Worker HSM: Frame completo 'CT' ({}  bytes): {}", packet.size(), ss.str());
        if (packet.size() != 37) {
            LOG_WARN("Worker HSM: AVISO — Frame 'CT' tem {} bytes, esperado 37! Verificar protocolo.", packet.size());
        }

        auto buf = std::make_shared<std::vector<uint8_t>>(packet);
        asio::async_write(*socket_, asio::buffer(*buf), [this, seq_id](asio::error_code ec, size_t) {
            if (ec) {
                LOG_ERROR("Worker HSM: Falha ao escrever no socket do HSM para Seq ID {}. Derrubando link...", seq_id);
                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_requests_.find(seq_id);
                if (it != pending_requests_.end()) {
                    if (it->second->on_complete) it->second->on_complete(nullptr);
                    pending_requests_.erase(it);
                }
                schedule_reconnect();
            }
        });

    } catch (const std::exception& e) {
        LOG_ERROR("Worker HSM: Erro ao construir pacote para o HSM para Seq ID {}: {}", seq_id, e.what());
        if (pending_req->on_complete) pending_req->on_complete(nullptr);
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.erase(seq_id);
    }
}

void HsmWorker::do_read_header() {
    read_buffer_.resize(2);
    asio::async_read(*socket_, asio::buffer(read_buffer_), asio::transfer_exactly(2), [this](asio::error_code ec, size_t) {
        if (!ec) {
            uint16_t length = (read_buffer_[0] << 8) | read_buffer_[1];
            if (length == 0 || length > 4096) {
                LOG_ERROR("Worker HSM: Recebido prefixo de tamanho anômalo ({} bytes). Link instável.", length);
                schedule_reconnect();
                return;
            }
            do_read_body(length);
        } else {
            LOG_ERROR("Worker HSM: Desconectado do HSM durante leitura do cabeçalho. Link caiu ({}).", ec.message());
            schedule_reconnect();
        }
    });
}

void HsmWorker::do_read_body(size_t body_length) {
    read_buffer_.resize(body_length);
    asio::async_read(*socket_, asio::buffer(read_buffer_), asio::transfer_exactly(body_length), [this](asio::error_code ec, size_t length) {
        if (!ec) {
            vector<uint8_t> payload(read_buffer_.begin(), read_buffer_.begin() + length);
            handle_hsm_response(payload);
            do_read_header();
        } else {
            LOG_ERROR("Worker HSM: Desconectado do HSM durante leitura do corpo. Link caiu.");
            schedule_reconnect();
        }
    });
}

void HsmWorker::handle_hsm_response(const vector<uint8_t>& payload) {
    stringstream ss;
    for(uint8_t b : payload) ss << setfill('0') << setw(2) << hex << (int)b;
    LOG_DEBUG("Worker HSM: Recebido pacote 'RT' do HSM. Hex: {}", ss.str());

    if (payload.size() < 5 || payload[0] != 'R' || payload[1] != 'T') {
        LOG_ERROR("Worker HSM: Resposta inválida do HSM. Tamanho: {}, Esperado >= 5 bytes.", payload.size());
        return;
    }

    uint16_t seq_id = (payload[2] << 8) | payload[3];
    uint8_t status = payload[4];

    shared_ptr<HsmPendingRequest> req;
    {
        lock_guard<mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(seq_id);
        if (it != pending_requests_.end()) {
            req = it->second;
            pending_requests_.erase(it);
        }
    }

    if (req) {
        if (status == 0x00 && payload.size() >= 21) {
            stringstream pin_out;
            for (size_t i = 5; i < 21; ++i) {
                pin_out << setfill('0') << setw(2) << hex << (int)payload[i];
            }
            string new_pin = pin_out.str();

            req->original_msg->set_field(52, new_pin);
            LOG_INFO("Worker HSM: Tradução de PIN bem-sucedida para Seq ID {}. Novo PIN: {}", seq_id, new_pin);

            req->on_complete(make_unique<IsoMessage>(*req->original_msg));
        } else {
            // Provide diagnostic info for the error
            stringstream error_details;
            error_details << "Status: 0x" << setfill('o') << setw(2) << hex << (int)status;
            error_details << ", Payload Size: " << payload.size() << " bytes";
            
            string status_description;
            switch(status) {
                case 0x01: status_description = "Erro de Parâmetro/Formato Inválido"; break;
                case 0x02: status_description = "Chave PEK não encontrada ou inválida"; break;
                case 0x03: status_description = "Bloco de PIN corrompido"; break;
                case 0x04: status_description = "Falha na criptografia"; break;
                default: status_description = "Erro desconhecido do HSM"; break;
            }
            
            LOG_ERROR("Worker HSM: FALHA na Tradução de PIN para Seq ID {}. {} - {}", seq_id, error_details.str(), status_description);
            req->on_complete(nullptr);
        }
    } else {
        LOG_WARN("Worker HSM: Recebida resposta para Seq ID {} não encontrado na fila de espera.", seq_id);
    }
}