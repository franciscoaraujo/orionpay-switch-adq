#include "pg_worker.h"
#include "../common/logger.h"
#include <iostream>
#include <pqxx/pqxx>
#include <optional>

using namespace std;

PgWorker& PgWorker::get_instance() {
    static PgWorker instance;
    return instance;
}

PgWorker::PgWorker() {}

PgWorker::~PgWorker() {
    if (worker_thread_.joinable()) {
        worker_thread_.request_stop();
        cv_.notify_all();
        worker_thread_.join();
    }
    LOG_INFO("Worker de Banco de Dados PostgreSQL: Desligamento completo.");
}

void PgWorker::start(const string& conn_str) {
    connection_string_ = conn_str;
    worker_thread_ = jthread([this](stop_token stoken) {
        this->worker_loop(stoken);
    });
    LOG_INFO("Worker de Banco de Dados PostgreSQL: Thread dedicada para inserções particionadas iniciada com sucesso.");
}

void PgWorker::enqueue_transaction(const TransactionLog& log) {
    {
        lock_guard<mutex> lock(queue_mutex_);
        log_queue_.push(log);
    }
    cv_.notify_one();
}

void PgWorker::worker_loop(stop_token stoken) {
    unique_ptr<pqxx::connection> conn;
    try {
        conn = make_unique<pqxx::connection>(connection_string_);
        LOG_INFO("Worker de Banco de Dados PostgreSQL: Conexão com o banco de dados estabelecida.");
        conn->prepare(
            "insert_transaction",
            "INSERT INTO switch.transactions ("
            "id, merchant_id, terminal_id, amount, currency_code, mti, pan_masked, "
            "nsu_adquirer, nsu_bandeira, response_code, created_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)"
        );
        LOG_INFO("Worker de Banco de Dados PostgreSQL: Declaração preparada 'insert_transaction' criada.");
    } catch (const pqxx::sql_error& e) {
        LOG_ERROR("Worker de Banco de Dados PostgreSQL: Erro de SQL durante conexão ou preparação: {}", e.what());
        return;
    } catch (const exception& e) {
        LOG_ERROR("Worker de Banco de Dados PostgreSQL: Erro geral durante conexão ou preparação: {}", e.what());
        return;
    }

    while (!stoken.stop_requested()) {
        TransactionLog log;
        bool has_log = false;

        {
            unique_lock<mutex> lock(queue_mutex_);
            cv_.wait(lock, stoken, [this]() { return !log_queue_.empty(); });

            if (stoken.stop_requested()) break;

            if (!log_queue_.empty()) {
                log = move(log_queue_.front());
                log_queue_.pop();
                has_log = true;
            }
        }

        if (has_log) {
            try {
                pqxx::work W(*conn);

                LOG_INFO("Worker de Banco de Dados PostgreSQL: Persistindo valor '{}' no banco de dados.", log.amount);

                W.exec_prepared(
                    "insert_transaction",
                    log.id,
                    log.merchant_id,
                    log.terminal_id,
                    log.amount,
                    log.currency_code,
                    log.mti,
                    log.pan_masked,
                    log.nsu_adquirer,
                    log.nsu_bandeira,
                    log.response_code,
                    log.created_at
                );
                W.commit();
                LOG_INFO("Worker de Banco de Dados PostgreSQL: Transação {} (MTI: {}) persistida com sucesso.", log.id, log.mti);
            } catch (const pqxx::sql_error& e) {
                LOG_ERROR("Worker de Banco de Dados PostgreSQL: Erro de SQL durante transação {}: {}", log.id, e.what());
            } catch (const exception& e) {
                LOG_ERROR("Worker de Banco de Dados PostgreSQL: Erro geral durante transação {}: {}", log.id, e.what());
            }
        }
    }
    LOG_INFO("Worker de Banco de Dados PostgreSQL: Loop do worker finalizado.");
}
