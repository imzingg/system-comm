#pragma once
#include <string>
#include <functional>
#include <memory>
#include <vector>

enum class FileStatus { COMPLETED, ERROR, TIMEOUT };

using FileCallback = std::function<void(FileStatus status, std::string local_path)>;
using DataCallback = std::function<void(std::string topic, std::string payload)>;

struct BridgeConfig {
    std::string my_id;          // e.g., "board_A"
    std::string router_ip;      // Zenoh router IP
    std::string download_dir;   // Directory to save downloaded files
    int file_transfer_rate_mbps = 5; // Bandwidth limit (5 MB/s)
};

class ZenohBridge {
public:
    ZenohBridge();
    ~ZenohBridge();

    bool init(const BridgeConfig& config);

    // 1. Data Plane (Fire-and-forget)
    // Internally maps to key: "sys/{my_id}/data/{topic}"
    void publish_data(const std::string& topic, const std::string& json_data);

    // Subscribes to: "sys/+/data/{topic}" to get data from any board
    void subscribe_data(const std::string& topic, DataCallback cb);

    // 2. File Plane (Async Request)
    // Sends request to target. Target sends file back via stream.
    void request_file(const std::string& target_id,
                      const std::string& remote_path,
                      FileCallback on_complete);

    // Enable this board to serve files when requested
    void enable_file_serving(bool enable);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
