#pragma once

#include <atomic>
#include <string>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream>

namespace common {

class StanGenerator {
public:
    static StanGenerator& get_instance() {
        static StanGenerator instance;
        return instance;
    }

    std::string get_next_stan() {
        uint32_t next_val = counter_.fetch_add(1);
        if (next_val > 999999) {
            counter_.store(1);
            next_val = 1;
        }

        save_state();

        std::stringstream ss;
        ss << std::setw(6) << std::setfill('0') << next_val;
        return ss.str();
    }

private:
    StanGenerator() {
        load_state();
    }

    void load_state() {
        std::ifstream file("stan.dat");
        if (file.is_open()) {
            uint32_t last_stan;
            file >> last_stan;
            counter_.store(last_stan + 1);
            file.close();
        } else {
            counter_.store(1);
        }
    }

    void save_state() {
        std::ofstream file("stan.dat");
        if (file.is_open()) {
            file << counter_.load();
            file.close();
        }
    }

    StanGenerator(const StanGenerator&) = delete;
    StanGenerator& operator=(const StanGenerator&) = delete;

    std::atomic<uint32_t> counter_;
};

} // namespace common