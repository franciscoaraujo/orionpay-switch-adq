#ifndef ORIONPAY_SWITCH_ADQ_TRANSACTION_ENTITY_H
#define ORIONPAY_SWITCH_ADQ_TRANSACTION_ENTITY_H

#include <string>
#include <optional>
#include <chrono> // For std::chrono::system_clock::time_point if needed, but std::string is often simpler for direct DB interaction

namespace persistence {

struct TransactionEntity {
    std::string id;
    std::string merchant_id;
    std::string terminal_id;
    std::string amount; // Using string to preserve exact numeric(12,2) precision
    int32_t currency_code;
    std::string mti;
    std::string pan_masked;
    std::string nsu_adquirer;
    std::optional<std::string> nsu_bandeira; // NULLable field
    std::string response_code;
    std::string created_at; // Using string for timestamptz for direct DB interaction

    // Default constructor
    TransactionEntity() = default;

    // Parameterized constructor for easy initialization
    TransactionEntity(
        const std::string& id,
        const std::string& merchant_id,
        const std::string& terminal_id,
        const std::string& amount,
        int32_t currency_code,
        const std::string& mti,
        const std::string& pan_masked,
        const std::string& nsu_adquirer,
        std::optional<std::string> nsu_bandeira,
        const std::string& response_code,
        const std::string& created_at)
        : id(id),
          merchant_id(merchant_id),
          terminal_id(terminal_id),
          amount(amount),
          currency_code(currency_code),
          mti(mti),
          pan_masked(pan_masked),
          nsu_adquirer(nsu_adquirer),
          nsu_bandeira(std::move(nsu_bandeira)),
          response_code(response_code),
          created_at(created_at) {}
};

} // namespace persistence

#endif //ORIONPAY_SWITCH_ADQ_TRANSACTION_ENTITY_H
