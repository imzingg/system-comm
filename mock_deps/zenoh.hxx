#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <iostream>
#include <mutex>

namespace zenoh {
    struct Config {
        void insert(const std::string& key, const std::string& val) {
            // Mock insert
        }
    };

    struct Bytes {
        std::vector<uint8_t> data_;
        Bytes(const std::vector<uint8_t>& d) : data_(d) {}
        Bytes(const std::string& s) : data_(s.begin(), s.end()) {}
        const uint8_t* data() const { return data_.data(); }
        size_t len() const { return data_.size(); }
    };

    struct Sample {
        struct Key {
            std::string k;
            std::string to_string() const { return k; }
        } key;

        std::vector<uint8_t> payload_data;
        Bytes get_payload() const { return Bytes(payload_data); }
    };

    // Forward decl
    namespace mock {
        extern std::vector<std::pair<std::string, std::string>> puts;
        extern std::vector<std::pair<std::string, std::vector<uint8_t>>> byte_puts;
        extern std::vector<std::pair<std::string, std::function<void(const Sample&)>>> subscribers;

        void reset();
        std::vector<std::pair<std::string, std::string>> get_puts();
        std::vector<std::pair<std::string, std::vector<uint8_t>>> get_byte_puts();
        void inject_subscriber_data(const std::string& k, const std::string& payload);
    }

    class Subscriber {
    public:
        Subscriber() = default;
        Subscriber(Subscriber&&) = default;
        Subscriber& operator=(Subscriber&&) = default;
    };

    struct Session {
        void put(const std::string& key, const std::string& val);
        void put(const std::string& key, const Bytes& bytes);
        Subscriber declare_subscriber(const std::string& key, std::function<void(const Sample&)> cb);
        void close() {}
        operator bool() const { return true; }
    };

    inline Session open(Config) { return Session(); }
}
