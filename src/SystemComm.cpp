#include "SystemComm.hpp"
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
class SystemComm::Impl {
public:
    Impl() = default;
    ~Impl() {
        if (session_) {
            // session_.close();
        }
    }

    bool init(const SystemCommConfig& config) {
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
                zconfig.insert("connect/endpoints", config_.router_ip);
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
        // Subscribe to both chunk and status using wildcard
        // Key format: sys/{my_id}/file/{req_id}/...
        std::string sub_key = "sys/" + config_.my_id + "/file/" + req_id + "/*";

        auto ctx = std::make_shared<FileRequestContext>();
        ctx->req_id = req_id;
        ctx->callback = on_complete;

        fs::path p(remote_path);
        ctx->local_path = fs::path(config_.download_dir) / p.filename();

        ctx->sub = session_.declare_subscriber(sub_key, [this, req_id](const zenoh::Sample& sample) {
            this->handle_file_response(req_id, sample);
        });

        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            active_requests_[req_id] = ctx;
        }

        // Send Request
        // Key: sys/{target_id}/file/req
        json req = {
            {"requester", config_.my_id},
            {"req_id", req_id},
            {"file_path", remote_path}
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

    void send_file_response(const std::string& requester_id, const std::string& req_id, const std::string& local_path) {
        // Run in thread to avoid blocking
        std::thread([this, requester_id, req_id, local_path]() {
            this->serve_file(requester_id, req_id, fs::path(local_path));
        }).detach();
    }

private:
    struct FileRequestContext {
        std::string req_id;
        fs::path local_path;
        std::ofstream ofs;
        FileCallback callback;
        zenoh::Subscriber sub;
        bool started = false;
    };

    SystemCommConfig config_;
    zenoh::Session session_;
    std::vector<zenoh::Subscriber> data_subs_;
    std::mutex subs_mutex_;

    std::mutex requests_mutex_;
    std::map<std::string, std::shared_ptr<FileRequestContext>> active_requests_;

    std::unique_ptr<zenoh::Subscriber> serving_sub_;

    void handle_file_response(const std::string& req_id, const zenoh::Sample& sample) {
        std::shared_ptr<FileRequestContext> ctx;
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            auto it = active_requests_.find(req_id);
            if (it == active_requests_.end()) return;
            ctx = it->second;
        }

        std::string key = sample.key.to_string();
        auto payload_bytes = sample.get_payload();

        if (key.find("/status") != std::string::npos) {
            // Handle Status
            std::string payload(reinterpret_cast<const char*>(payload_bytes.data()), payload_bytes.len());
            try {
                auto j = json::parse(payload);
                std::string state = j["state"];

                if (state == "START") {
                    ctx->ofs.open(ctx->local_path, std::ios::binary);
                    if (!ctx->ofs) {
                        std::cerr << "Failed to open file for writing: " << ctx->local_path << std::endl;
                        ctx->callback(FileStatus::ERROR, "");
                        cleanup_request(req_id);
                        return;
                    }
                    ctx->started = true;
                } else if (state == "COMPLETED") {
                    ctx->ofs.close();
                    ctx->callback(FileStatus::COMPLETED, ctx->local_path.string());
                    cleanup_request(req_id);
                } else if (state == "ERROR") {
                    ctx->ofs.close();
                    ctx->callback(FileStatus::ERROR, "");
                    cleanup_request(req_id);
                }
            } catch (const std::exception& e) {
                std::cerr << "JSON Parse Error in Status: " << e.what() << std::endl;
            }
        } else if (key.find("/chunk") != std::string::npos) {
            // Handle Chunk
            // Payload is pure binary data

            if (!ctx->started || !ctx->ofs.is_open()) {
                // If we missed START, try to open?
                if (!ctx->started) {
                     ctx->ofs.open(ctx->local_path, std::ios::binary);
                     ctx->started = true;
                }
            }
            if (ctx->ofs.is_open()) {
                 const char* data_ptr = reinterpret_cast<const char*>(payload_bytes.data());
                 ctx->ofs.write(data_ptr, payload_bytes.len());
            }
        }
    }

    void cleanup_request(const std::string& req_id) {
         // Cleanup (Async to avoid destroying subscriber inside its callback)
        std::thread([this, req_id]() {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            active_requests_.erase(req_id);
        }).detach();
    }

    void handle_file_request(const zenoh::Sample& sample) {
        try {
            auto payload_bytes = sample.get_payload();
            std::string payload(reinterpret_cast<const char*>(payload_bytes.data()), payload_bytes.len());
            auto j = json::parse(payload);

            // Format: {"requester": "board_A", "req_id": "...", "file_path": "..."}
            std::string requester_id = j.value("requester", "");
            std::string req_id = j["req_id"];
            std::string path_str = j["file_path"];

            if (requester_id.empty()) return;

            fs::path safe_root(config_.download_dir);

            // Check if path is absolute, if so, we might need to be careful.
            // Let's strip leading slashes to force relative.
            while (!path_str.empty() && path_str[0] == '/') {
                path_str = path_str.substr(1);
            }

            fs::path requested_path = safe_root / path_str;

            if (path_str.find("..") != std::string::npos) {
                // Potential traversal
                send_status(requester_id, req_id, "ERROR");
                return;
            }

            if (fs::exists(requested_path) && fs::is_regular_file(requested_path)) {
                // Serve file using the new internal/external API logic
                // Pass path directly, as we already verified it.
                // But wait, send_file_response takes a string.
                this->send_file_response(requester_id, req_id, requested_path.string());
            } else {
                 send_status(requester_id, req_id, "ERROR");
            }

        } catch (const std::exception& e) {
            std::cerr << "File Request Error: " << e.what() << std::endl;
        }
    }

    void send_status(const std::string& requester_id, const std::string& req_id, const std::string& state, size_t size = 0) {
        json status = { {"state", state} };
        if (state == "START") {
            status["size"] = size;
        }
        std::string key = "sys/" + requester_id + "/file/" + req_id + "/status";
        session_.put(key, status.dump());
    }

    void serve_file(std::string requester_id, std::string req_id, fs::path filepath) {
        std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);
        if (!ifs) {
            send_status(requester_id, req_id, "ERROR");
            return;
        }

        size_t file_size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        // Send START
        send_status(requester_id, req_id, "START", file_size);

        TokenBucket bucket(config_.file_transfer_rate_mbps);
        std::vector<char> buffer(64 * 1024); // 64KB chunks
        std::string chunk_key = "sys/" + requester_id + "/file/" + req_id + "/chunk";

        while (ifs) {
            ifs.read(buffer.data(), buffer.size());
            std::streamsize bytes_read = ifs.gcount();
            if (bytes_read <= 0) break;

            bucket.consume(static_cast<size_t>(bytes_read));

            // Publish Chunk - Raw Binary Data
            std::vector<uint8_t> chunk_data(bytes_read);
            std::memcpy(chunk_data.data(), buffer.data(), bytes_read);

            session_.put(chunk_key, zenoh::Bytes(chunk_data));
        }

        // Send COMPLETED
        send_status(requester_id, req_id, "COMPLETED");
    }
};

// SystemComm Wrapper Implementation
SystemComm::SystemComm() : impl_(std::make_unique<Impl>()) {}
SystemComm::~SystemComm() = default;

bool SystemComm::init(const SystemCommConfig& config) {
    return impl_->init(config);
}

void SystemComm::publish_data(const std::string& topic, const std::string& json_data) {
    impl_->publish_data(topic, json_data);
}

void SystemComm::subscribe_data(const std::string& topic, DataCallback cb) {
    impl_->subscribe_data(topic, cb);
}

void SystemComm::request_file(const std::string& target_id, const std::string& remote_path, FileCallback on_complete) {
    impl_->request_file(target_id, remote_path, on_complete);
}

void SystemComm::enable_file_serving(bool enable) {
    impl_->enable_file_serving(enable);
}

void SystemComm::send_file_response(const std::string& requester_id, const std::string& req_id, const std::string& local_path) {
    impl_->send_file_response(requester_id, req_id, local_path);
}
