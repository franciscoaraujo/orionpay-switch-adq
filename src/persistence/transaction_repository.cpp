#include "transaction_repository.h"
#include "../common/logger.h"
#include "../database/pg_worker.h" // Include PgWorker to access TransactionLog and enqueue_transaction

namespace persistence {

TransactionRepository& TransactionRepository::get_instance() {
    static TransactionRepository instance;
    return instance;
}

TransactionRepository::TransactionRepository() {
    LOG_INFO("TransactionRepository initialized for database persistence.");
}

void TransactionRepository::save(const TransactionEntity& transaction) {
    // Convert TransactionEntity to TransactionLog for PgWorker
    TransactionLog log;
    log.id = transaction.id;
    log.merchant_id = transaction.merchant_id;
    log.terminal_id = transaction.terminal_id;
    log.amount = transaction.amount;
    log.currency_code = transaction.currency_code;
    log.mti = transaction.mti;
    log.pan_masked = transaction.pan_masked;
    log.nsu_adquirer = transaction.nsu_adquirer;
    // Directly assign the optional value
    log.nsu_bandeira = transaction.nsu_bandeira;
    log.response_code = transaction.response_code;
    log.created_at = transaction.created_at;

    // Enqueue the transaction for asynchronous persistence via PgWorker
    PgWorker::get_instance().enqueue_transaction(log);

    LOG_INFO("Transaction {} enqueued for database persistence.", transaction.id);
}

} // namespace persistence
