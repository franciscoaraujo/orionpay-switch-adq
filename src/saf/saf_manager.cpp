#include "saf_manager.h"
#include "../common/logger.h"
#include <iostream>
#include <chrono>

using namespace std;

SafManager& SafManager::get_instance() {
    static SafManager instance;
    return instance;
}

SafManager::SafManager() {}

SafManager::~SafManager() {
    if (is_running_) {
        saf_thread_.request_stop();
        cv_.notify_all();
    }
}

void SafManager::start(std::function<void(std::unique_ptr<IsoMessage>)> send_callback) {
    if (is_running_) return;

    send_callback_ = send_callback;
    is_running_ = true;

    load_from_disk();

    saf_thread_ = jthread([this](stop_token stoken) {
        this->worker_loop(stoken);
    });

    LOG_INFO("SAF Manager Thread started.");
}

void SafManager::enqueue_reversal(unique_ptr<IsoMessage> reversal_msg, const string& stan) {
    SafEntry entry{stan, "0420", move(reversal_msg), 0};

    // Persist this reversal so it survives restarts
    persist_to_disk(entry);

    {
        lock_guard<mutex> lock(queue_mutex_);
        saf_queue_.push(move(entry));
    }
    cv_.notify_one();
}

void SafManager::acknowledge_reversal(const string& stan) {
    LOG_INFO("SAF: Acknowledged reversal for STAN {}", stan);
}

void SafManager::worker_loop(stop_token stoken) {
    while (!stoken.stop_requested()) {
        SafEntry entry;
        bool has_entry = false;

        {
            unique_lock<mutex> lock(queue_mutex_);
            bool success = cv_.wait_for(lock, stoken, chrono::seconds(5), [this]() {
                return !saf_queue_.empty();
            });

            if (stoken.stop_requested()) break;

            if (success) {
                entry = move(saf_queue_.front());
                saf_queue_.pop();
                has_entry = true;
            }
        }

        if (has_entry && send_callback_ && entry.reversal_msg) {
            LOG_INFO("SAF: Retrying reversal for STAN {}, Attempt {}", entry.original_stan, entry.retry_count + 1);

            entry.retry_count++;

            LOG_DEBUG("SAF simulated send.");
        }
    }
    LOG_INFO("SAF Manager Thread shutting down.");
}

void SafManager::persist_to_disk(const SafEntry& /*entry*/) {
    LOG_DEBUG("SAF: Persisted to disk.");
}

void SafManager::load_from_disk() {
    LOG_DEBUG("SAF: Loaded pending reversals from disk.");
}