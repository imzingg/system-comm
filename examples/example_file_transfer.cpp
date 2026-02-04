#include "SystemComm.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

void run_provider(const std::string& board_id) {
    SystemCommConfig config;
    config.my_id = board_id;
    config.router_ip = "127.0.0.1";
    config.download_dir = "./provider_files_" + board_id;
    config.file_transfer_rate_mbps = 100;

    // Create a dummy file to serve
    if (!fs::exists(config.download_dir)) {
        fs::create_directories(config.download_dir);
    }
    std::string filename = "test_data.bin";
    std::string filepath = config.download_dir + "/" + filename;
    {
        std::ofstream ofs(filepath, std::ios::binary);
        for(int i=0; i<1024*1024; ++i) { // 1MB file
            ofs << (char)(i % 256);
        }
    }
    std::cout << "Created test file at: " << filepath << std::endl;

    SystemComm comm;
    if (!comm.init(config)) {
        std::cerr << "Failed to init provider" << std::endl;
        return;
    }

    // Enable file serving
    comm.enable_file_serving(true);
    std::cout << "[" << board_id << "] Provider running. Waiting for requests..." << std::endl;

    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void run_requester(const std::string& board_id, const std::string& target_id) {
    SystemCommConfig config;
    config.my_id = board_id;
    config.router_ip = "127.0.0.1";
    config.download_dir = "./requester_downloads_" + board_id;

    if (!fs::exists(config.download_dir)) {
        fs::create_directories(config.download_dir);
    }

    SystemComm comm;
    if (!comm.init(config)) {
        std::cerr << "Failed to init requester" << std::endl;
        return;
    }

    std::cout << "[" << board_id << "] Requester initialized." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait a bit for discovery

    std::string remote_file = "test_data.bin";
    std::cout << "[" << board_id << "] Requesting " << remote_file << " from " << target_id << "..." << std::endl;

    bool done = false;
    comm.request_file(target_id, remote_file, [&](FileStatus status, std::string path) {
        if (status == FileStatus::COMPLETED) {
            std::cout << "[" << board_id << "] SUCCESS: File downloaded to " << path << std::endl;
        } else {
            std::cout << "[" << board_id << "] FAILURE: File transfer failed." << std::endl;
        }
        done = true;
    });

    while(!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  Provider: " << argv[0] << " provider <board_id>" << std::endl;
        std::cerr << "  Requester: " << argv[0] << " requester <board_id> <target_board_id>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    std::string board_id = argv[2];

    if (mode == "provider") {
        run_provider(board_id);
    } else if (mode == "requester") {
        if (argc < 4) {
            std::cerr << "Requester needs target board ID" << std::endl;
            return 1;
        }
        std::string target_id = argv[3];
        run_requester(board_id, target_id);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }

    return 0;
}
