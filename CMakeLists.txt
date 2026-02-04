cmake_minimum_required(VERSION 3.10)
project(ZenohBridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory
include_directories(include)

# Find dependencies
find_package(zenohcxx QUIET)
if (NOT zenohcxx_FOUND)
    message(STATUS "zenohcxx not found, assuming standard include paths")
endif()

find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, assuming standard include paths")
endif()

# Define library
add_library(ZenohBridge src/ZenohBridge.cpp)

# Link libraries if targets exist
if (TARGET zenohcxx::zenohcxx)
    target_link_libraries(ZenohBridge PRIVATE zenohcxx::zenohcxx)
endif()

if (TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(ZenohBridge PRIVATE nlohmann_json::nlohmann_json)
endif()
