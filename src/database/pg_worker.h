#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <optional> // For std::optional
#include "../iso8583/iso_message.h"

// Estrutura que reflete a tabela pai do PostgreSQL
struct TransactionLog {
    std::string id;            // UUID
    std::string merchant_id;
    std::string terminal_id;
    std::string amount;
    int32_t currency_code; // Changed from string to int32_t to match TransactionEntity
    std::string mti;
    std::string pan_masked;
    std::string nsu_adquirer;
    std::optional<std::string> nsu_bandeira; // Changed to std::optional
    std::string response_code;
    std::string created_at;    // Timestamp nativo em string ou time_point
};

class PgWorker {
public:
    static PgWorker& get_instance();

    PgWorker(const PgWorker&) = delete;
    PgWorker& operator=(const PgWorker&) = delete;

    void start(const std::string& conn_str);

    // O Event Loop (ou a rotina que recebeu 0110 ou 0420) joga aqui para gravar em background
    void enqueue_transaction(const TransactionLog& log);

private:
    PgWorker();
    ~PgWorker();

    void worker_loop(std::stop_token stoken);
    // void insert_db(const TransactionLog& log); // Removed

    std::queue<TransactionLog> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable_any cv_;

    std::jthread worker_thread_;
    std::string connection_string_;
};
