cmake_minimum_required(VERSION 3.10)
project(ZenohBridgeTest)

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

# Test Executable
add_executable(zenoh_bridge_test
    tests/test_zenoh_bridge.cpp
    src/ZenohBridge.cpp
    mock_deps/zenoh_mock.cpp
)

target_link_libraries(zenoh_bridge_test gtest_main pthread)
