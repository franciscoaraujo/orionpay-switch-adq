#pragma once
#include <asio.hpp>
#include <memory>
#include <functional> // Para std::function
#include <vector>     // Para buffer dinâmico do payload
#include <cstdint>    // Para uint16_t, uint8_t
#include <array>      // Para std::array para o header

#include "../common/thread_pool.h"
#include "acquirer.pb.h" // Inclui as mensagens Protobuf geradas

using namespace std;
using asio::ip::tcp;

// Define o tipo para o callback de processamento de autorização.
// Ele recebe um AuthorizeRequest e retorna um AuthorizeResponse.
using AuthProcessingCallback = std::function<orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse(
    const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest&)>;

class Session : public enable_shared_from_this<Session> {
public:
    // O construtor agora recebe o callback de processamento de autorização
    Session(tcp::socket socket, shared_ptr<ThreadPool> thread_pool, AuthProcessingCallback auth_callback);
    void start();

private:
    // Funções para gerenciar o ciclo de leitura e escrita
    void read_header();
    void handle_read_header(const asio::error_code& error, size_t bytes_transferred);
    void read_payload(uint16_t payload_size);
    void handle_read_payload(const asio::error_code& error, size_t bytes_transferred);
    void process_request(std::string request_payload_str); // Processa a requisição deserializada
    void do_write(const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse& response);
    void handle_write(const asio::error_code& error, size_t bytes_transferred);

    // Membros da classe
    tcp::socket socket_;
    shared_ptr<ThreadPool> thread_pool_;
    AuthProcessingCallback auth_callback_; // Callback para a lógica de negócio

    // Buffer para o cabeçalho de 2 bytes (tamanho do payload)
    std::array<uint8_t, 2> header_buffer_;
    // Buffer dinâmico para o payload da mensagem
    std::vector<uint8_t> payload_buffer_;
};
