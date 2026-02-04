#include "zenoh.hxx"

namespace zenoh {
    namespace mock {
        std::vector<std::pair<std::string, std::string>> puts;
        std::vector<std::pair<std::string, std::vector<uint8_t>>> byte_puts;
        std::vector<std::pair<std::string, std::function<void(const Sample&)>>> subscribers;

        void reset() {
            puts.clear();
            byte_puts.clear();
            subscribers.clear();
        }

        std::vector<std::pair<std::string, std::string>> get_puts() { return puts; }
        std::vector<std::pair<std::string, std::vector<uint8_t>>> get_byte_puts() { return byte_puts; }

        void inject_subscriber_data(const std::string& k, const std::string& payload) {
             for(auto& sub : subscribers) {
                 Sample s;
                 s.key.k = k;
                 s.payload_data = std::vector<uint8_t>(payload.begin(), payload.end());
                 sub.second(s);
             }
        }
    }

    void Session::put(const std::string& key, const std::string& val) {
        mock::puts.push_back({key, val});
    }
    void Session::put(const std::string& key, const Bytes& bytes) {
         mock::byte_puts.push_back({key, bytes.data_});
         std::string s(bytes.data_.begin(), bytes.data_.end());
         mock::puts.push_back({key, s});
    }
    Subscriber Session::declare_subscriber(const std::string& key, std::function<void(const Sample&)> cb) {
        mock::subscribers.push_back({key, cb});
        return Subscriber();
    }
}
