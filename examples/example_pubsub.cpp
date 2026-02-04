#include "SystemComm.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <board_id>" << std::endl;
        return 1;
    }

    std::string board_id = argv[1];

    SystemCommConfig config;
    config.my_id = board_id;
    config.router_ip = "127.0.0.1"; // Default to localhost router for example
    config.download_dir = "./downloads_" + board_id;

    SystemComm comm;
    if (!comm.init(config)) {
        std::cerr << "Failed to initialize SystemComm" << std::endl;
        return 1;
    }

    std::cout << "[" << board_id << "] SystemComm Initialized." << std::endl;

    // Subscribe to 'chat' topic
    comm.subscribe_data("chat", [board_id](std::string topic, std::string payload) {
        std::cout << "[" << board_id << "] Received on " << topic << ": " << payload << std::endl;
    });

    std::cout << "Subscribed to 'chat'. Publishing messages..." << std::endl;

    int counter = 0;
    while (true) {
        std::string msg = "Message " + std::to_string(counter++) + " from " + board_id;
        std::string json_payload = "{\"msg\": \"" + msg + "\"}";

        comm.publish_data("chat", json_payload);
        std::cout << "[" << board_id << "] Published: " << msg << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
