#include "ZenohBridge.hpp"
#include <zenoh.hxx>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <random>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Helper: Generate UUID
std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        int val = dis(gen);
        if (val < 10) ss << val;
        else ss << (char)('a' + val - 10);
    }
    return ss.str();
}

// Helper: TokenBucket
class TokenBucket {
public:
    TokenBucket(size_t rate_mbps)
        : rate_bytes_per_sec_(rate_mbps * 1024 * 1024),
          // Ensure capacity is at least 64KB to allow large chunks
          max_tokens_(std::max(rate_bytes_per_sec_, static_cast<size_t>(64 * 1024))),
          tokens_(max_tokens_),
          last_refill_(std::chrono::steady_clock::now()) {}

    void consume(size_t bytes) {
        if (rate_bytes_per_sec_ == 0) return; // No limit

        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            refill();
            if (tokens_ >= bytes) {
                tokens_ -= bytes;
                return;
            }

            // Not enough tokens, calculate wait time
            double missing = static_cast<double>(bytes - tokens_);
            double wait_seconds = missing / rate_bytes_per_sec_;

            lock.unlock();
            std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
            lock.lock();
        }
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last_refill_;
        size_t new_tokens = static_cast<size_t>(elapsed.count() * rate_bytes_per_sec_);

        if (new_tokens > 0) {
            tokens_ = std::min(tokens_ + new_tokens, max_tokens_);
            last_refill_ = now;
        }
    }

    size_t rate_bytes_per_sec_;
    size_t max_tokens_;
    size_t tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;
};

// Impl Class
class ZenohBridge::Impl {
public:
    Impl() = default;
    ~Impl() {
        // Close session and stop threads if needed
        if (session_) {
            // session_.close(); // Zenoh session usually closes on destruction
        }
    }

    bool init(const BridgeConfig& config) {
        config_ = config;

        try {
            if (!config_.download_dir.empty() && !fs::exists(config_.download_dir)) {
                fs::create_directories(config_.download_dir);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error creating download directory: " << e.what() << std::endl;
            return false;
        }

        try {
            zenoh::Config zconfig;
            if (!config_.router_ip.empty()) {
                // zconfig.insert("connect/endpoints", config_.router_ip);
            }
            session_ = zenoh::open(std::move(zconfig));
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Zenoh init failed: " << e.what() << std::endl;
            return false;
        }
    }

    void publish_data(const std::string& topic, const std::string& json_data) {
        std::string key = "sys/" + config_.my_id + "/data/" + topic;
        session_.put(key, json_data);
    }

    void subscribe_data(const std::string& topic, DataCallback cb) {
        std::string key = "sys/+/data/" + topic;
        auto sub = session_.declare_subscriber(key, [cb](const zenoh::Sample& sample) {
            auto payload_bytes = sample.get_payload();
            std::string payload(reinterpret_cast<const char*>(payload_bytes.data()), payload_bytes.len());
            cb(sample.key.to_string(), payload);
        });

        std::lock_guard<std::mutex> lock(subs_mutex_);
        data_subs_.push_back(std::move(sub));
    }

    void request_file(const std::string& target_id, const std::string& remote_path, FileCallback on_complete) {
        std::string req_id = generate_uuid();
        std::string resp_key = "sys/" + config_.my_id + "/file/resp/" + req_id;

        auto ctx = std::make_shared<FileRequestContext>();
        ctx->req_id = req_id;
        ctx->callback = on_complete;

        fs::path p(remote_path);
        ctx->local_path = fs::path(config_.download_dir) / p.filename();

        ctx->ofs.open(ctx->local_path, std::ios::binary);
        if (!ctx->ofs) {
            on_complete(FileStatus::ERROR, "");
            return;
        }

        ctx->sub = session_.declare_subscriber(resp_key, [this, req_id](const zenoh::Sample& sample) {
            this->handle_file_chunk(req_id, sample);
        });

        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            active_requests_[req_id] = ctx;
        }

        json req = {
            {"req_id", req_id},
            {"path", remote_path},
            {"requester_id", config_.my_id}
        };
        std::string req_key = "sys/" + target_id + "/file/req";
        session_.put(req_key, req.dump());
    }

    void enable_file_serving(bool enable) {
        if (enable) {
            if (serving_sub_) return;
            std::string key = "sys/" + config_.my_id + "/file/req";
            serving_sub_ = std::make_unique<zenoh::Subscriber>(
                session_.declare_subscriber(key, [this](const zenoh::Sample& sample) {
                    this->handle_file_request(sample);
                })
            );
        } else {
            serving_sub_.reset();
        }
    }

private:
    struct FileRequestContext {
        std::string req_id;
        fs::path local_path;
        std::ofstream ofs;
        FileCallback callback;
        zenoh::Subscriber sub;
    };

    BridgeConfig config_;
    zenoh::Session session_;
    std::vector<zenoh::Subscriber> data_subs_;
    std::mutex subs_mutex_;

    std::mutex requests_mutex_;
    std::map<std::string, std::shared_ptr<FileRequestContext>> active_requests_;

    std::unique_ptr<zenoh::Subscriber> serving_sub_;

    void handle_file_chunk(const std::string& req_id, const zenoh::Sample& sample) {
        std::shared_ptr<FileRequestContext> ctx;
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            auto it = active_requests_.find(req_id);
            if (it == active_requests_.end()) return;
            ctx = it->second;
        }

        auto payload_bytes = sample.get_payload();

        if (payload_bytes.len() < 4) {
            return;
        }

        size_t data_len = payload_bytes.len() - 4;

        if (data_len == 0) {
            // EOF
            ctx->ofs.close();
            ctx->callback(FileStatus::COMPLETED, ctx->local_path.string());

            std::thread([this, req_id]() {
                std::lock_guard<std::mutex> lock(requests_mutex_);
                active_requests_.erase(req_id);
            }).detach();
            return;
        }

        const char* data_ptr = reinterpret_cast<const char*>(payload_bytes.data()) + 4;
        ctx->ofs.write(data_ptr, data_len);
    }

    void handle_file_request(const zenoh::Sample& sample) {
        try {
            auto payload_bytes = sample.get_payload();
            std::string payload(reinterpret_cast<const char*>(payload_bytes.data()), payload_bytes.len());
            auto j = json::parse(payload);

            std::string req_id = j["req_id"];
            std::string path_str = j["path"];
            std::string requester_id = j.value("requester_id", "");

            if (requester_id.empty()) {
                return;
            }

            fs::path safe_root(config_.download_dir);
            fs::path requested_path = safe_root / path_str;

            if (path_str.find("..") != std::string::npos) {
                return;
            }

            if (fs::exists(requested_path) && fs::is_regular_file(requested_path)) {
                std::thread([this, requester_id, req_id, requested_path]() {
                    this->serve_file(requester_id, req_id, requested_path);
                }).detach();
            }

        } catch (const std::exception& e) {
            std::cerr << "File Request Error: " << e.what() << std::endl;
        }
    }

    void serve_file(std::string requester_id, std::string req_id, fs::path filepath) {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs) return;

        TokenBucket bucket(config_.file_transfer_rate_mbps);
        std::vector<char> buffer(64 * 1024); // 64KB chunks
        uint32_t seq = 0;
        std::string resp_key = "sys/" + requester_id + "/file/resp/" + req_id;

        while (ifs) {
            ifs.read(buffer.data(), buffer.size());
            std::streamsize bytes_read = ifs.gcount();
            if (bytes_read <= 0) break;

            bucket.consume(static_cast<size_t>(bytes_read));

            // Prepare payload
            std::vector<uint8_t> payload(4 + bytes_read);
            std::memcpy(payload.data(), &seq, 4); // Seq ID
            std::memcpy(payload.data() + 4, buffer.data(), bytes_read);

            session_.put(resp_key, zenoh::Bytes(payload));

            seq++;
        }

        // Send EOF
        std::vector<uint8_t> eof(4);
        std::memcpy(eof.data(), &seq, 4);
        session_.put(resp_key, zenoh::Bytes(eof));
    }
};

// ZenohBridge Wrapper Implementation
ZenohBridge::ZenohBridge() : impl_(std::make_unique<Impl>()) {}
ZenohBridge::~ZenohBridge() = default;

bool ZenohBridge::init(const BridgeConfig& config) {
    return impl_->init(config);
}

void ZenohBridge::publish_data(const std::string& topic, const std::string& json_data) {
    impl_->publish_data(topic, json_data);
}

void ZenohBridge::subscribe_data(const std::string& topic, DataCallback cb) {
    impl_->subscribe_data(topic, cb);
}

void ZenohBridge::request_file(const std::string& target_id, const std::string& remote_path, FileCallback on_complete) {
    impl_->request_file(target_id, remote_path, on_complete);
}

void ZenohBridge::enable_file_serving(bool enable) {
    impl_->enable_file_serving(enable);
}
