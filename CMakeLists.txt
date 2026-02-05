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

# Main Library
add_library(SystemComm src/SystemComm.cpp)

# Find dependencies
# Note: In a real environment, these should be installed.
find_package(zenohcxx QUIET)
find_package(zenohc QUIET) # Added as requested
find_package(nlohmann_json QUIET)

# Configuration for REAL environment
if (zenohcxx_FOUND AND zenohc_FOUND AND nlohmann_json_FOUND)
    # As requested: link zenohcxx::zenohc and nlohmann_json
    # Note: zenoh-cpp documentation says zenohcxx depends on zenohc.
    # The user asked for "zenohcxx::zenohc".
    target_link_libraries(SystemComm PUBLIC zenohcxx::zenohc nlohmann_json::nlohmann_json)
else()
    # Fallback for Sandbox/Test environment (Mocking)
    message(STATUS "Dependencies not found. Using MOCKS for build/test.")

    # Define mock targets if they don't exist to simulate the real targets
    if (NOT TARGET zenohcxx::zenohc)
        add_library(zenohcxx::zenohc INTERFACE IMPORTED)
        set_target_properties(zenohcxx::zenohc PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/mock_deps"
        )
    endif()

    if (NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/mock_deps"
        )
    endif()

    target_link_libraries(SystemComm PUBLIC zenohcxx::zenohc nlohmann_json::nlohmann_json)

    # For the mock implementation source (needed for linking in sandbox)
    target_sources(SystemComm PRIVATE mock_deps/zenoh_mock.cpp)
endif()

# Test Executable
add_executable(system_comm_test tests/test_system_comm.cpp)
target_link_libraries(system_comm_test SystemComm gtest_main pthread)

# Examples
add_executable(example_publisher examples/example_publisher.cpp)
target_link_libraries(example_publisher SystemComm pthread)

add_executable(example_subscriber examples/example_subscriber.cpp)
target_link_libraries(example_subscriber SystemComm pthread)

add_executable(example_file_transfer examples/example_file_transfer.cpp)
target_link_libraries(example_file_transfer SystemComm pthread)
