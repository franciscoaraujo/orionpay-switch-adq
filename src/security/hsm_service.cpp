#include "hsm_service.h"
#include "../common/logger.h"
#include "../config/config_manager.h"
#include "soft_hsm.h"

HsmService& HsmService::get() {
    static HsmService instance;
    return instance;
}

bool HsmService::verify_pin(const string& pan, const string& pin_block) {
    // If no PIN block provided, treat as signature-based (no PIN validation needed)
    if (pin_block.empty()) {
        return true;
    }
    
    // Get ZPK from configuration
    const auto& config = ConfigManager::getInstance().getConfig();
    std::string zpk_hex = config.value("/hsm/zpk_hex"_json_pointer, 
                                       std::string("0123456789ABCDEFFEDCBA9876543210"));
    
    // Decrypt the PIN block using the SoftHSM
    auto decrypted_pin = SoftHsm::get_instance().decrypt_pin_block_iso0(pin_block, pan, zpk_hex);
    
    if (!decrypted_pin.has_value()) {
        LOG_ERROR("HSM Service: Failed to decrypt PIN block. PIN validation failed.");
        return false;
    }
    
    // PIN successfully decrypted - this validates that the PIN block format is correct
    // and was encrypted with the correct key
    LOG_INFO("HSM Service: PIN block decrypted successfully. PIN length: {}", decrypted_pin.value().length());
    return true;
}

string HsmService::tokenize_pan(const string& pan) {
    lock_guard<mutex> lock(mtx_);
    if (pan_map_.find(pan) != pan_map_.end()) {
        return pan_map_[pan];
    }

    string token = to_string(token_counter_++);
    pan_map_[pan] = token;
    token_map_[token] = pan;
    LOG_DEBUG("Tokenized PAN. Token: {}", token);
    return token;
}

string HsmService::detokenize(const string& token) {
    lock_guard<mutex> lock(mtx_);
    if (token_map_.find(token) != token_map_.end()) {
        return token_map_[token];
    }
    return "";
}
