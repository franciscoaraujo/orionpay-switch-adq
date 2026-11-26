#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <asio.hpp>
#include <map>
#include <future> // Required for std::promise and std::future
#include "../iso8583/iso_message.h"

using HsmResultCallback = std::function<void(std::unique_ptr<IsoMessage>)>;

struct HsmJob {
    std::unique_ptr<IsoMessage> msg;
    HsmResultCallback on_complete;
};

struct HsmPendingRequest {
    std::shared_ptr<IsoMessage> original_msg;
    HsmResultCallback on_complete;
};

class HsmWorker {
public:
    static HsmWorker& get_instance();

    HsmWorker(const HsmWorker&) = delete;
    HsmWorker& operator=(const HsmWorker&) = delete;

    void start(const std::string& hsm_ip = "127.0.0.1", short hsm_port = 15001);

    // Asynchronous method
    void enqueue_job(std::unique_ptr<IsoMessage> msg, HsmResultCallback callback);

    // New synchronous method for PIN translation
    std::string translate_pin_sync(const std::string& pin_block, const std::string& pan);

private:
    HsmWorker();
    ~HsmWorker();

    void worker_loop(std::stop_token stoken);

    void connect_hsm();
    void schedule_reconnect();
    void do_read_header();
    void do_read_body(size_t body_length);
    void handle_hsm_response(const std::vector<uint8_t>& payload);

    void perform_pin_translation(HsmJob job);
    std::vector<uint8_t> hex_to_bytes(const std::string& hex);

    std::queue<HsmJob> job_queue_;
    std::mutex queue_mutex_;
    std::condition_variable_any cv_;

    std::jthread worker_thread_;
    std::atomic<bool> is_running_{false};

    std::unique_ptr<asio::io_context> io_context_;
    std::unique_ptr<asio::ip::tcp::socket> socket_;
    std::unique_ptr<asio::steady_timer> reconnect_timer_;
    std::thread io_thread_;
    std::string hsm_ip_;
    short hsm_port_;
    std::string source_pek_kcv_;
    int source_pek_index_;
    int destination_pek_index_;
    std::string zpk_hex_;
    std::atomic<bool> hsm_connected_{false};
    std::vector<uint8_t> read_buffer_;

    uint16_t current_sequence_id_{1};
    std::map<uint16_t, std::shared_ptr<HsmPendingRequest>> pending_requests_;
    std::mutex pending_mutex_;
};