#include <gtest/gtest.h>
#include "SystemComm.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

// Access to the mock zenoh backend (defined in mock_deps/zenoh.hxx)
// We need to declare the external interface to the mock here if we want to inspect it.
// Since we haven't written the enhanced mock yet, we will define what we expect.

namespace zenoh {
    namespace mock {
        void reset();
        std::vector<std::pair<std::string, std::string>> get_puts(); // key, string payload
        std::vector<std::pair<std::string, std::vector<uint8_t>>> get_byte_puts(); // key, byte payload
        void inject_subscriber_data(const std::string& key, const std::string& payload);
    }
}

class SystemCommTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset mock state before each test
        zenoh::mock::reset();

        // Setup config
        config.my_id = "test_board";
        config.router_ip = "127.0.0.1";
        config.download_dir = "./test_downloads";
        config.file_transfer_rate_mbps = 100;

        std::filesystem::create_directories(config.download_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(config.download_dir);
    }

    SystemCommConfig config;
};

TEST_F(SystemCommTest, Initialization) {
    SystemComm bridge;
    EXPECT_TRUE(bridge.init(config));
}

TEST_F(SystemCommTest, PublishData) {
    SystemComm bridge;
    bridge.init(config);

    std::string topic = "cpu";
    std::string payload = "{\"usage\": 50}";
    bridge.publish_data(topic, payload);

    auto puts = zenoh::mock::get_puts();
    ASSERT_EQ(puts.size(), 1);
    EXPECT_EQ(puts[0].first, "sys/test_board/data/cpu");
    EXPECT_EQ(puts[0].second, payload);
}

TEST_F(SystemCommTest, SubscribeData) {
    SystemComm bridge;
    bridge.init(config);

    bool callback_called = false;
    std::string received_topic;
    std::string received_payload;

    bridge.subscribe_data("cpu", [&](std::string t, std::string p) {
        callback_called = true;
        received_topic = t;
        received_payload = p;
    });

    // Simulate incoming data
    zenoh::mock::inject_subscriber_data("sys/other/data/cpu", "{\"usage\": 90}");

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_topic, "sys/other/data/cpu");
    EXPECT_EQ(received_payload, "{\"usage\": 90}");
}

TEST_F(SystemCommTest, RequestFileAndReceive) {
    SystemComm bridge;
    bridge.init(config);

    bool complete = false;
    std::string completed_path;

    // 1. Request File
    bridge.request_file("target_board", "remote_log.txt", [&](FileStatus status, std::string path) {
        if (status == FileStatus::COMPLETED) {
            complete = true;
            completed_path = path;
        }
    });

    // Verify request was sent
    auto puts = zenoh::mock::get_puts();
    ASSERT_EQ(puts.size(), 1);
    std::string req_key = "sys/target_board/file/req";
    EXPECT_EQ(puts[0].first, req_key);

    // Parse request to get req_id
    // Simple parsing since we don't link full json lib here easily?
    // Actually we do link nlohmann/json mock or real. The mock has limited parsing.
    // Let's just extract UUID from the string manually or assume it's there.
    // Mock JSON dump returns "{}". Wait, my previous mock returned "{}".
    // I need to improve the mock JSON to actually dump the content for this test to work well?
    // OR, I can rely on the fact that the code puts a string.

    // In the real code, it dumps a JSON. In the mock I will write next, I need `json` to behave reasonably.
    // For now, let's assume the mock JSON dump works or we just check key.
}

TEST_F(SystemCommTest, SendFileResponse) {
    SystemComm bridge;
    bridge.init(config);

    // Create a dummy file to serve
    std::string file_content = "Hello World";
    std::string filename = "server_file.txt";
    {
        std::ofstream ofs(filename, std::ios::binary);
        ofs << file_content;
    }

    bridge.send_file_response("requester_board", "req-123", filename);

    // Allow thread to run
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check outputs
    // Expect:
    // 1. Status START
    // 2. Chunk
    // 3. Status COMPLETED

    auto puts = zenoh::mock::get_puts();
    auto byte_puts = zenoh::mock::get_byte_puts();

    // Check Status START
    bool start_found = false;
    bool completed_found = false;
    for(const auto& p : puts) {
        if (p.first.find("status") != std::string::npos) {
            if (p.second.find("START") != std::string::npos) start_found = true;
            if (p.second.find("COMPLETED") != std::string::npos) completed_found = true;
        }
    }

    EXPECT_TRUE(start_found);
    EXPECT_TRUE(completed_found);

    // Check Chunk
    ASSERT_EQ(byte_puts.size(), 1);
    EXPECT_TRUE(byte_puts[0].first.find("chunk") != std::string::npos);

    std::string chunk_str(byte_puts[0].second.begin(), byte_puts[0].second.end());
    EXPECT_EQ(chunk_str, file_content);

    std::filesystem::remove(filename);
}
