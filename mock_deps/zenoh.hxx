#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <iostream>
#include <mutex>

namespace zenoh {
    struct Config {};

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

        inline void reset() {
            puts.clear();
            byte_puts.clear();
            subscribers.clear();
        }

        inline std::vector<std::pair<std::string, std::string>> get_puts() { return puts; }
        inline std::vector<std::pair<std::string, std::vector<uint8_t>>> get_byte_puts() { return byte_puts; }

        inline void inject_subscriber_data(const std::string& k, const std::string& payload) {
             for(auto& sub : subscribers) {
                 // Simple wildcard match? Or exact match for now.
                 // Zenoh wildcards are complex. Let's match if sub key is substring or exact.
                 // If sub key has '+', we assume regex match needed, but for mock let's just trigger all.
                 Sample s;
                 s.key.k = k;
                 s.payload_data = std::vector<uint8_t>(payload.begin(), payload.end());
                 sub.second(s);
             }
        }
    }

    class Subscriber {
    public:
        Subscriber() = default;
        Subscriber(Subscriber&&) = default;
        Subscriber& operator=(Subscriber&&) = default;
    };

    struct Session {
        void put(const std::string& key, const std::string& val) {
            mock::puts.push_back({key, val});
        }
        void put(const std::string& key, const Bytes& bytes) {
             mock::byte_puts.push_back({key, bytes.data_});
             // Also store as string for easier debugging if it's text
             std::string s(bytes.data_.begin(), bytes.data_.end());
             mock::puts.push_back({key, s});
        }
        Subscriber declare_subscriber(const std::string& key, std::function<void(const Sample&)> cb) {
            mock::subscribers.push_back({key, cb});
            return Subscriber();
        }
        void close() {}
        operator bool() const { return true; }
    };

    inline Session open(Config) { return Session(); }
}
