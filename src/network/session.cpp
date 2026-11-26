#include "session.h"
#include "../common/logger.h"
// #include "../switch/router.h" // Não é mais necessário diretamente na sessão
// #include "../iso8583/iso_parser.h" // Não é mais necessário
#include <vector>
#include <string> // Para std::string
#include <algorithm> // Para std::copy
#include <asio/buffer.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

// Funções auxiliares para conversão de byte order (Big Endian)
// Assumindo que o sistema host é Little Endian para a troca de bytes.
// Se o sistema host for Big Endian, essas funções seriam no-ops.
// Para portabilidade real, usar asio::detail::socket_ops::host_to_network_short
// ou implementar com detecção de endianness. Para este cenário, uma simples troca é suficiente.
uint16_t host_to_network_short(uint16_t value) {
    return (value << 8) | (value >> 8);
}

uint16_t network_to_host_short(uint16_t value) {
    return (value << 8) | (value >> 8);
}

Session::Session(tcp::socket socket, shared_ptr<ThreadPool> thread_pool, AuthProcessingCallback auth_callback)
    : socket_(move(socket)),
      thread_pool_(thread_pool),
      auth_callback_(std::move(auth_callback)) {
}

void Session::start() {
    read_header();
}

void Session::read_header() {
    auto self(shared_from_this());
    asio::async_read(socket_, asio::buffer(header_buffer_),
        [this, self](const asio::error_code& error, size_t bytes_transferred) {
            handle_read_header(error, bytes_transferred);
        });
}

void Session::handle_read_header(const asio::error_code& error, size_t bytes_transferred) {
    if (!error) {
        if (bytes_transferred != 2) {
            LOG_ERROR("Received incomplete header ({} bytes). Closing connection.", bytes_transferred);
            socket_.close();
            return;
        }

        // Converte os 2 bytes do cabeçalho para um uint16_t (Network Byte Order -> Host Byte Order)
        uint16_t payload_size_network;
        std::memcpy(&payload_size_network, header_buffer_.data(), 2);
        uint16_t payload_size = network_to_host_short(payload_size_network);

        LOG_DEBUG("Received header. Next payload size: {} bytes", payload_size);

        if (payload_size == 0) {
            LOG_WARN("Received zero-length payload. Closing connection.");
            socket_.close();
            return;
        }

        read_payload(payload_size);
    } else if (error == asio::error::eof || error == asio::error::connection_reset) {
        LOG_DEBUG("Client disconnected gracefully during header read.");
    } else {
        LOG_ERROR("Error reading header: {}", error.message());
    }
    // Em caso de erro, a conexão é implicitamente fechada ou será tratada pelo caller.
    // Para simplificar, fechamos a conexão em qualquer erro de leitura.
    if (error) {
        socket_.close();
    }
}

void Session::read_payload(uint16_t payload_size) {
    payload_buffer_.resize(payload_size); // Redimensiona o buffer para o tamanho do payload
    auto self(shared_from_this());
    asio::async_read(socket_, asio::buffer(payload_buffer_),
        [this, self, payload_size](const asio::error_code& error, size_t bytes_transferred) {
            handle_read_payload(error, bytes_transferred);
        });
}

void Session::handle_read_payload(const asio::error_code& error, size_t bytes_transferred) {
    if (!error) {
        if (bytes_transferred != payload_buffer_.size()) {
            LOG_ERROR("Received incomplete payload (expected {} bytes, got {}). Closing connection.", payload_buffer_.size(), bytes_transferred);
            socket_.close();
            return;
        }

        // Converte o payload para std::string para o Protobuf
        std::string request_payload_str(payload_buffer_.begin(), payload_buffer_.end());

        LOG_DEBUG("Received payload ({} bytes). Processing request...", bytes_transferred);

        // Processa a requisição em um thread do pool para não bloquear o io_context
        thread_pool_->enqueue([self = shared_from_this(), request_payload_str]() {
            self->process_request(request_payload_str);
        });

    } else if (error == asio::error::eof || error == asio::error::connection_reset) {
        LOG_DEBUG("Client disconnected gracefully during payload read.");
    } else {
        LOG_ERROR("Error reading payload: {}", error.message());
    }
    if (error) {
        socket_.close();
    }
}

void Session::process_request(std::string request_payload_str) {
    orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest request;
    orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse response;

    if (!request.ParseFromString(request_payload_str)) {
        LOG_ERROR("Failed to parse AuthorizeRequest from payload.");
        // Em caso de erro de parsing, enviar uma resposta de erro ou fechar a conexão
        response.set_transaction_id("");
        response.set_status_code("96"); // System Error
        response.set_description("Failed to parse request");
        // A escrita ainda precisa ser feita no io_context thread
        asio::post(socket_.get_executor(), [self = shared_from_this(), response]() {
            self->do_write(response);
        });
        return;
    }

    LOG_INFO("Processing AuthorizeRequest for transaction_id: {}", request.id());

    try {
        // Chama o callback de processamento de autorização
        response = auth_callback_(request);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during authorization processing: {}", e.what());
        response.set_transaction_id(request.id());
        response.set_status_code("96"); // System Error
        response.set_description(fmt::format("Internal server error: {}", e.what()));
    }

    // A escrita da resposta deve ser agendada de volta no io_context thread
    asio::post(socket_.get_executor(), [self = shared_from_this(), response]() {
        self->do_write(response);
    });
}

void Session::do_write(const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse& response) {
    std::string response_payload_str;
    if (!response.SerializeToString(&response_payload_str)) {
        LOG_ERROR("Failed to serialize AuthorizeResponse.");
        socket_.close(); // Não é possível enviar a resposta
        return;
    }

    uint16_t payload_size = static_cast<uint16_t>(response_payload_str.length());
    uint16_t header_value = host_to_network_short(payload_size);

    // Move payload para um shared_ptr para garantir que a memória permaneça viva
    auto response_buf = std::make_shared<std::string>(std::move(response_payload_str));
    // Header em buffer compartilhado para evitar dangling pointer
    auto header_buf = std::make_shared<std::array<uint8_t, 2>>();
    (*header_buf)[0] = static_cast<uint8_t>((header_value >> 8) & 0xFF);
    (*header_buf)[1] = static_cast<uint8_t>(header_value & 0xFF);

    std::array<asio::const_buffer, 2> buffers;
    buffers[0] = asio::buffer(header_buf->data(), 2); // Cabeçalho de 2 bytes
    buffers[1] = asio::buffer(*response_buf); // Payload

    auto self(shared_from_this());
    // Captura response_buf e header_buf para mantê-los vivos até o fim do async_write
    asio::async_write(socket_, buffers,
        [this, self, response_buf, header_buf](const asio::error_code& error, size_t bytes_transferred) {
            handle_write(error, bytes_transferred);
        });
}

void Session::handle_write(const asio::error_code& error, size_t bytes_transferred) {
    if (!error) {
        LOG_DEBUG("Response sent ({} bytes). Closing connection.", bytes_transferred);
    } else {
        LOG_ERROR("Error writing response: {}", error.message());
    }
    // Após enviar a resposta (ou em caso de erro na escrita), fechar a conexão.
    socket_.close();
}
