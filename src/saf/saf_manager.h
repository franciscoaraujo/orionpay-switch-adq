#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include "../iso8583/iso_message.h"

// Explicit declaration of the alias type. Using this in the class directly.
using SendMessageCallback = std::function<void(std::unique_ptr<IsoMessage>)>;

// Estrutura que representa uma mensagem retida no SAF (Store and Forward)
struct SafEntry {
    std::string original_stan;
    std::string original_mti;
    std::unique_ptr<IsoMessage> reversal_msg; // O MTI 0420 gerado
    int retry_count = 0;
};

class SafManager {
public:
    static SafManager& get_instance();

    SafManager(const SafManager&) = delete;
    SafManager& operator=(const SafManager&) = delete;

    // Inicializa a thread de background que tentará esvaziar a fila periodicamente
    void start(std::function<void(std::unique_ptr<IsoMessage>)> send_callback);

    // Enfileira um desfazimento (0420) quando o Event Loop detectar Timeout
    void enqueue_reversal(std::unique_ptr<IsoMessage> reversal_msg, const std::string& stan);

    // Marca que um acatamento (0430) chegou da VisaNet, removendo a mensagem da fila
    void acknowledge_reversal(const std::string& stan);

private:
    SafManager();
    ~SafManager();

    void worker_loop(std::stop_token stoken);

    // Função auxiliar para tentar salvar os MTI 0420 em um SQLite/Arquivo local
    // garantindo que não se percam num restart.
    void persist_to_disk(const SafEntry& entry);
    void load_from_disk();

    std::queue<SafEntry> saf_queue_;
    std::mutex queue_mutex_;
    std::condition_variable_any cv_;

    std::jthread saf_thread_;
    std::atomic<bool> is_running_{false};

    // Injetado do ciclo de vida superior, chama visanet_client->send_message()
    std::function<void(std::unique_ptr<IsoMessage>)> send_callback_;
};