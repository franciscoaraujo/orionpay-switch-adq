#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include "../iso8583/iso_message.h"

// Callback de resposta. Se o ponteiro for null, significa que ocorreu um TIMEOUT.
using ResponseCallback = std::function<void(std::unique_ptr<IsoMessage>)>;

// Estrutura para rastrear requisições em voo e seus timers de timeout
struct PendingRequest {
    std::shared_ptr<asio::steady_timer> timer;
    std::shared_ptr<IsoMessage> original_msg;
    ResponseCallback on_response;
};

class VisaNetClient : public std::enable_shared_from_this<VisaNetClient> {
public:
    static std::shared_ptr<VisaNetClient> create(asio::io_context& io_context, const std::string& host, short port);

    void start();

    // Envia a transação. O Timeout configurado disparará o SAF se a VisaNet não responder.
    void send_message(std::unique_ptr<IsoMessage> msg, ResponseCallback on_response);

    ~VisaNetClient();

private:
    VisaNetClient(asio::io_context& io_context, const std::string& host, short port);

    void do_connect();
    void do_read_header();
    void do_read_body(size_t body_length);
    void handle_response(const std::vector<uint8_t>& raw_data);
    void do_write(std::shared_ptr<std::vector<uint8_t>> data);

    // Regras de Negócio de Timeout e Keep-Alive
    void handle_timeout(const std::string& stan);
    void start_echo_timer();
    void send_echo_message();

    asio::ip::tcp::socket socket_;
    asio::ip::tcp::resolver resolver_;
    asio::steady_timer echo_timer_;
    std::string host_;
    short port_;
    std::atomic<bool> connected_{false};

    std::vector<uint8_t> read_buffer_;

    // Tabela Hash para correlacionar Requisições com Respostas (Baseado no STAN - Campo 11)
    std::map<std::string, std::shared_ptr<PendingRequest>> pending_requests_;
    std::mutex pending_mutex_;
};