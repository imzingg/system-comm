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

# Main Library (if you want to build it separately)
add_library(SystemComm src/SystemComm.cpp)
# Link dependencies...

# Test Executable
add_executable(system_comm_test
    tests/test_system_comm.cpp
    src/SystemComm.cpp
    mock_deps/zenoh_mock.cpp
)

target_link_libraries(system_comm_test gtest_main pthread)
