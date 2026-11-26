#include "visanet_client.h"
#include "../common/logger.h"
#include "../saf/saf_manager.h"
#include "../iso8583/iso_parser.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace std;

shared_ptr<VisaNetClient> VisaNetClient::create(
    asio::io_context& io_context, 
    const string& host, 
    short port
) {
    return shared_ptr<VisaNetClient>(new VisaNetClient(io_context, host, port));
}

VisaNetClient::VisaNetClient(
    asio::io_context& io_context, 
    const string& host, 
    short port
)
    : socket_(io_context), resolver_(io_context), echo_timer_(io_context), host_(host), port_(port) {
}

VisaNetClient::~VisaNetClient() {
    if (socket_.is_open()) {
        asio::error_code ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}

void VisaNetClient::start() {
    do_connect();
}

void VisaNetClient::do_connect() {
    auto endpoints = resolver_.resolve(host_, to_string(port_));
    auto self(shared_from_this());

    asio::async_connect(socket_, endpoints,
        [this, self](asio::error_code ec, asio::ip::tcp::endpoint) {
            if (!ec) {
                connected_ = true;
                LOG_INFO("Roteador de Saída VisaNet: Conectado com sucesso ao endpoint externo da VisaNet {}:{}", host_, port_);
                do_read_header();
            } else {
                LOG_ERROR("Roteador de Saída VisaNet: Falha ao conectar à VisaNet: {}. Acionando fallback/retry...", ec.message());
            }
        });
}

void VisaNetClient::start_echo_timer() {}
void VisaNetClient::send_echo_message() {}

void VisaNetClient::send_message(unique_ptr<IsoMessage> msg, ResponseCallback on_response) {
    string stan = msg->get_field(11);
    if (stan.empty()) {
        LOG_ERROR("Roteador de Saída VisaNet: Não é possível despachar transação sem STAN (Campo 11)");
        if (on_response) on_response(nullptr);
        return;
    }

    LOG_DEBUG("Roteador de Saída VisaNet: Despachando transação para a VisaNet. STAN: {}", stan);

    auto timer = make_shared<asio::steady_timer>(socket_.get_executor());
    timer->expires_after(chrono::seconds(15)); // Reduced from 15 to 4 to prevent gRPC DEADLINE_EXCEEDED

    auto request_ctx = make_shared<PendingRequest>(PendingRequest{
        timer,
        shared_ptr<IsoMessage>(new IsoMessage(*msg)),
        on_response
    });

    {
        lock_guard<mutex> lock(pending_mutex_);
        pending_requests_[stan] = request_ctx;
    }

    auto self(shared_from_this());
    timer->async_wait([this, self, stan](asio::error_code ec) {
        if (!ec) {
            handle_timeout(stan);
        }
    });

    auto serialized_data = std::make_shared<std::vector<uint8_t>>(msg->serialize());

    // Debug: log outbound payload hex to assist correlation debugging
    std::stringstream ss_out;
    for (uint8_t b : *serialized_data) {
        ss_out << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    LOG_DEBUG("VisaNet Outbound Router: Sending payload ({} bytes) to VisaNet. Hex: {}", serialized_data->size(), ss_out.str());

    uint16_t length = serialized_data->size();
    serialized_data->insert(serialized_data->begin(), (length >> 8) & 0xFF);
    serialized_data->insert(serialized_data->begin() + 1, length & 0xFF);

    // If the transport is currently connected, write immediately.
    if (connected_) {
        do_write(serialized_data);
        return;
    }

    // Transport is down: attempt limited reconnection retries with exponential backoff
    const int max_attempts = 3;
    auto attempts = make_shared<int>(0);
    auto retry_timer = make_shared<asio::steady_timer>(socket_.get_executor());
    // Use a shared_ptr to allow the lambda to call itself recursively
    auto try_send = make_shared<std::function<void(void)>>();

    *try_send = [this, self, serialized_data, retry_timer, attempts, max_attempts, try_send, stan]() {
        if (connected_) {
            LOG_INFO("Roteador de Saída VisaNet: Link restaurado — enviando STAN {}", stan);
            do_write(serialized_data);
            return;
        }

        if (*attempts >= max_attempts) {
            LOG_ERROR("Roteador de Saída VisaNet: Link CAIU após {} tentativas. Acionando geração IMEDIATA de SAF/0420 para o STAN {}", max_attempts, stan);
            auto reversal_msg = make_unique<IsoMessage>();
            reversal_msg->set_mti("0420");
            reversal_msg->set_field(11, stan);
            SafManager::get_instance().enqueue_reversal(move(reversal_msg), stan);

            // Clean up pending request and notify caller of failure
            shared_ptr<PendingRequest> req;
            {
                lock_guard<mutex> lock(pending_mutex_);
                auto it = pending_requests_.find(stan);
                if (it != pending_requests_.end()) { req = it->second; pending_requests_.erase(it); }
            }
            if (req && req->on_response) req->on_response(nullptr);
            return;
        }

        (*attempts)++;
        LOG_WARN("Roteador de Saída VisaNet: Link down — tentativa de reconexão {}/{} para STAN {}", *attempts, max_attempts, stan);

        // Trigger async connect attempt
        do_connect();

        // Exponential backoff: 500ms, 1000ms, 2000ms
        int wait_ms = 500 * (1 << (*attempts - 1));
        retry_timer->expires_after(chrono::milliseconds(wait_ms));
        auto self_inner = shared_from_this();
        retry_timer->async_wait([try_send, self_inner](asio::error_code ec) {
            if (!ec) {
                (*try_send)();
            }
        });
    };

    // Start the first try (will call do_connect and possibly schedule further attempts)
    (*try_send)();
}

void VisaNetClient::handle_timeout(const string& stan) {
    shared_ptr<PendingRequest> req;
    {
        lock_guard<mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(stan);
        if (it != pending_requests_.end()) {
            req = it->second;
            pending_requests_.erase(it);
        }
    }

    if (req) {
        LOG_WARN("Roteador de Saída VisaNet: TIMEOUT para o STAN {}. Acionando Desfazimento SAF (0420).", stan);
        auto reversal_msg = make_unique<IsoMessage>();
        reversal_msg->set_mti("0420");
        reversal_msg->set_field(11, stan);
        reversal_msg->set_field(39, "68");
        SafManager::get_instance().enqueue_reversal(move(reversal_msg), stan);
        if (req->on_response) {
            req->on_response(nullptr);
        }
    }
}

void VisaNetClient::do_write(shared_ptr<vector<uint8_t>> data) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(*data),
        [this, self](asio::error_code ec, size_t) {
            if (ec) {
                LOG_ERROR("Roteador de Saída VisaNet: Erro de Escrita na Rede: {}. Link desconectado.", ec.message());
                connected_ = false;
                socket_.close();
            }
        });
}

void VisaNetClient::do_read_header() {
    auto self(shared_from_this());
    read_buffer_.assign(2, 0);

    asio::async_read(socket_, asio::buffer(read_buffer_, 2),
        [this, self](asio::error_code ec, size_t length) {
            if (!ec && length == 2) {
                size_t body_length = (read_buffer_[0] << 8) | read_buffer_[1];
                do_read_body(body_length);
            } else {
                LOG_ERROR("Roteador de Saída VisaNet: Erro de Leitura do Cabeçalho: {}. Link desconectado.", ec.message());
                connected_ = false;
                socket_.close();
            }
        });
}

void VisaNetClient::do_read_body(size_t body_length) {
    auto self(shared_from_this());
    read_buffer_.assign(body_length, 0);

    asio::async_read(socket_, asio::buffer(read_buffer_, body_length),
        [this, self, body_length](asio::error_code ec, size_t length) {
            if (!ec && length == body_length) {
                handle_response(read_buffer_);
                do_read_header();
            } else {
                LOG_ERROR("Roteador de Saída VisaNet: Erro de Leitura do Corpo: {}. Link desconectado.", ec.message());
                connected_ = false;
                socket_.close();
            }
        });
}

void VisaNetClient::handle_response(const vector<uint8_t>& raw_data) {
    std::stringstream ss;
    for (uint8_t b : raw_data) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    LOG_INFO("Roteador de Saída VisaNet: Recebido payload ({} bytes) da VisaNet. Hex: {}", raw_data.size(), ss.str());

    try {
        auto incoming_msg = std::make_unique<IsoMessage>(IsoParser::parse(raw_data));

        // Log parsed fields for debugging correlation issues
        std::stringstream parsed_ss;
        for (const auto& p : incoming_msg->fields) {
            parsed_ss << p.first << "=" << p.second << ";";
        }
        LOG_DEBUG("VisaNet Outbound Router: Parsed incoming fields: {}", parsed_ss.str());

        string mti = incoming_msg->get_mti();
        string stan = incoming_msg->get_field(11);

        // Validate critical fields
        if (mti.empty()) {
            LOG_ERROR("Roteador de Saída VisaNet: Resposta inválida - MTI ausente. Descartando.");
            return;
        }
        if (stan.empty() && mti != "0810" && mti != "0430") {
            LOG_ERROR("Roteador de Saída VisaNet: Resposta inválida - STAN ausente para MTI {}. Descartando.", mti);
            return;
        }

        if (mti == "0430") {
            if (!stan.empty()) {
                SafManager::get_instance().acknowledge_reversal(stan);
            } else {
                LOG_WARN("Roteador de Saída VisaNet: Recebida Resposta de Desfazimento (0430) sem STAN. Ignoring.");
            }
            return;
        }
        if (mti == "0810") {
            LOG_INFO("Roteador de Saída VisaNet: Recebida Resposta de Echo (0810). Link está saudável.");
            return;
        }

        shared_ptr<PendingRequest> req;
        {
            lock_guard<mutex> lock(pending_mutex_);
            auto it = pending_requests_.find(stan);
            if (it != pending_requests_.end()) {
                req = it->second;
                req->timer->cancel();
                pending_requests_.erase(it);
            }
        }

        if (req && req->on_response) {
            LOG_INFO("Roteador de Saída VisaNet: Resposta MTI {} correlacionada ao STAN {}. Retomando o Roteador.", mti, stan);
            req->on_response(move(incoming_msg));
        } else {
            // Provide diagnostic info for orphaned responses
            if (stan.empty()) {
                LOG_WARN("Roteador de Saída VisaNet: Recebida resposta MTI {} sem STAN (possível malformação). Descartando.", mti);
            } else {
                LOG_WARN("Roteador de Saída VisaNet: Recebida resposta MTI {} para STAN {} não encontrado em pending_requests (possível timeout anterior). Descartando.", mti, stan);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Roteador de Saída VisaNet: Falha ao fazer o parse da resposta ISO 8583: {}", e.what());
    }
}
