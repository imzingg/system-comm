cmake_minimum_required(VERSION 3.10)
project(SystemComm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Includes
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/mock_deps)

# Main Library
add_library(SystemComm src/SystemComm.cpp)

# Find dependencies (Mock or Real)
find_package(zenohcxx QUIET)
if (NOT zenohcxx_FOUND)
    message(STATUS "zenohcxx not found, using mock_deps")
    include_directories(mock_deps)
endif()

find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, using mock_deps")
    include_directories(mock_deps)
endif()


# Test Executable
add_executable(system_comm_test
    tests/test_system_comm.cpp
    mock_deps/zenoh_mock.cpp
)
target_link_libraries(system_comm_test SystemComm gtest_main pthread)

# Examples
add_executable(example_publisher examples/example_publisher.cpp mock_deps/zenoh_mock.cpp)
target_link_libraries(example_publisher SystemComm pthread)

add_executable(example_subscriber examples/example_subscriber.cpp mock_deps/zenoh_mock.cpp)
target_link_libraries(example_subscriber SystemComm pthread)

add_executable(example_file_transfer examples/example_file_transfer.cpp mock_deps/zenoh_mock.cpp)
target_link_libraries(example_file_transfer SystemComm pthread)
