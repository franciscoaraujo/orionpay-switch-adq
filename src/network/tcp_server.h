#pragma once
#include <asio.hpp>
#include <memory>
#include <functional> // Para std::function

#include "session.h" // Inclui a nova definição de Session
#include "../common/thread_pool.h" // Inclui o ThreadPool

using namespace std;
using asio::ip::tcp;

class TcpServer {
public:
    // Construtor que recebe o io_context, a porta e o callback de processamento de autorização
    TcpServer(asio::io_context& io_context, short port, shared_ptr<ThreadPool> thread_pool, AuthProcessingCallback auth_callback);

    void start();

private:
    void do_accept();

    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    shared_ptr<ThreadPool> thread_pool_;
    AuthProcessingCallback auth_callback_; // Callback para a lógica de negócio
};
