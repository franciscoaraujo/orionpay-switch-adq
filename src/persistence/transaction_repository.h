#ifndef ORIONPAY_SWITCH_ADQ_TRANSACTION_REPOSITORY_H
#define ORIONPAY_SWITCH_ADQ_TRANSACTION_REPOSITORY_H

#include "transaction_entity.h"
#include "../database/pg_worker.h"
#include "../common/logger.h"
#include <string>
#include <memory> // For std::shared_ptr

namespace persistence {

class TransactionRepository {
public:
    // Singleton access
    static TransactionRepository& get_instance();

    // Method to save a TransactionEntity to the database
    void save(const TransactionEntity& transaction);

private:
    // Private constructor for singleton pattern
    TransactionRepository();
    // Private destructor
    ~TransactionRepository() = default;

    // Delete copy constructor and assignment operator to prevent copying
    TransactionRepository(const TransactionRepository&) = delete;
    TransactionRepository& operator=(const TransactionRepository&) = delete;
};

} // namespace persistence

#endif //ORIONPAY_SWITCH_ADQ_TRANSACTION_REPOSITORY_H
