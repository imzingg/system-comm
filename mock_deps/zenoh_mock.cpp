#include "zenoh.hxx"

namespace zenoh {
    namespace mock {
        std::vector<std::pair<std::string, std::string>> puts;
        std::vector<std::pair<std::string, std::vector<uint8_t>>> byte_puts;
        std::vector<std::pair<std::string, std::function<void(const Sample&)>>> subscribers;
    }
}
