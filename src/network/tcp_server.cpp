#include "tcp_server.h"
#include "../common/logger.h" // Para LOG_INFO, LOG_ERROR

TcpServer::TcpServer(asio::io_context& io_context, short port, shared_ptr<ThreadPool> thread_pool, AuthProcessingCallback auth_callback)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
      thread_pool_(thread_pool),
      auth_callback_(std::move(auth_callback)) { // Move o callback para o membro da classe
    LOG_INFO("TCP Server listening on port {}", port);
}

void TcpServer::start() {
    do_accept();
}

void TcpServer::do_accept() {
    acceptor_.async_accept(
        [this](asio::error_code ec, tcp::socket socket) {
            if (!ec) {
                LOG_INFO("Accepted new connection from {}", socket.remote_endpoint().address().to_string());
                // Cria uma nova sessão para cada conexão aceita
                // Passa o socket, o thread pool e o callback de autorização
                std::make_shared<Session>(std::move(socket), thread_pool_, auth_callback_)->start();
            } else {
                LOG_ERROR("Error accepting connection: {}", ec.message());
            }

            // Continua aceitando novas conexões
            do_accept();
        });
}
