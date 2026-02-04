# SystemComm

SystemComm is a high-performance C++ wrapper library for Eclipse Zenoh, designed to simplify telemetry data exchange and asynchronous file transfer between embedded systems.

## Features

*   **Telemetry Data Plane**: High-throughput Pub/Sub for system metrics using JSON.
*   **File Transfer Plane**:
    *   **Asynchronous**: Non-blocking file request and download stream.
    *   **Rate Limiting**: Token Bucket algorithm to control bandwidth usage (configurable in MBps).
    *   **Reliable Protocol**: Custom Request/Response protocol over Zenoh Pub/Sub to ensure data integrity and isolation using UUIDs.
    *   **Security**: Path traversal protection to restrict file access to a safe directory.
*   **Thread Safety**: Internal worker threads handle file I/O and network streaming.

## Architecture

### Zenoh Key Design

The library uses a structured key space to manage communication:

| Purpose | Key Pattern | Direction | Description |
|---|---|---|---|
| **Data** | `sys/{my_id}/data/{topic}` | Pub | Telemetry data publication |
| **Data** | `sys/+/data/{topic}` | Sub | Telemetry data subscription |
| **File Req** | `sys/{target_id}/file/req` | Pub | Request a file from a target |
| **File Status** | `sys/{requester}/file/{req_id}/status` | Sub | Status updates (START, COMPLETED, ERROR) |
| **File Chunk** | `sys/{requester}/file/{req_id}/chunk` | Sub | Binary file data chunks |

### File Transfer Protocol

1.  **Request**: Requester generates a UUID (`req_id`) and sends a JSON request to the Target.
2.  **Handshake**: Target checks file existence and security, then sends `START` status with file size.
3.  **Streaming**: Target streams file chunks (binary) at the configured rate limit.
4.  **Completion**: Target sends `COMPLETED` status. Requester closes the file.

## Dependencies

*   C++17 or later
*   [Eclipse Zenoh C++ API (zenoh-cxx)](https://github.com/eclipse-zenoh/zenoh-cxx)
*   [nlohmann/json](https://github.com/nlohmann/json)
*   [GoogleTest](https://github.com/google/googletest) (for unit tests)

## Build Instructions

```bash
mkdir build && cd build
cmake ..
make
```

To build with tests:

```bash
cmake .. -DENABLE_TESTS=ON
make
./system_comm_test
```

## Usage Example

### Initialization

```cpp
#include "SystemComm.hpp"

BridgeConfig config;
config.my_id = "board_A";
config.router_ip = "192.168.1.101"; // Optional, defaults to multicast
config.download_dir = "./downloads";
config.file_transfer_rate_mbps = 5;

SystemComm comm;
if (!comm.init(config)) {
    // Handle error
}
```

### Telemetry (Pub/Sub)

```cpp
// Publisher
comm.publish_data("cpu_usage", "{\"val\": 45.2}");

// Subscriber
comm.subscribe_data("cpu_usage", [](std::string topic, std::string payload) {
    std::cout << "Received on " << topic << ": " << payload << std::endl;
});
```

### File Transfer (Requester)

```cpp
// Request 'log.txt' from 'board_B'
comm.request_file("board_B", "log.txt", [](FileStatus status, std::string path) {
    if (status == FileStatus::COMPLETED) {
        std::cout << "File downloaded to: " << path << std::endl;
    } else {
        std::cerr << "Transfer failed!" << std::endl;
    }
});
```

### File Transfer (Provider)

```cpp
// Enable this board to serve files when requested
comm.enable_file_serving(true);

// Files will be served from the 'download_dir' configured in init.
// The library automatically handles incoming requests safely.
```

### Manual File Sending

If you need to manually trigger a file send (e.g., custom logic):

```cpp
comm.send_file_response("board_A", "req-uuid-1234", "local/path/to/file.bin");
```

## Configuration

Sample Zenoh router configurations are provided in the `config/` directory for a setup with two boards:

*   **Board A**: `config/zenoh_router_A.json5` (192.168.1.101:7447)
*   **Board B**: `config/zenoh_router_B.json5` (192.168.1.102:7447)
