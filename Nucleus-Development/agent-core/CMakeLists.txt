cmake_minimum_required(VERSION 3.15)
project(agent-core VERSION 0.1.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID}")

# Compiler flags
if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find required packages
find_package(CURL REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add cmake modules to path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Find ZeroMQ
include(FindZeroMQ)

# Source files
set(AGENT_CORE_SOURCES
    src/main.cpp
    src/config/config_json.cpp
    src/identity/identity.cpp
    src/net/net_path_selector.cpp
    src/net/https_client.cpp
    src/auth/auth_manager.cpp
    src/reg/registration_ssm.cpp
    src/mqtt/mqtt_client.cpp
    src/bus/zmq_bus.cpp
    src/bus/envelope_serialization.cpp
    src/ext/extension_manager.cpp
    src/res/resource_monitor.cpp
    src/service/restart_manager.cpp
    src/service/restart_state_store.cpp
    src/util/retry.cpp
    src/util/uuid.cpp
    src/telemetry/logging.cpp
    src/telemetry/metrics.cpp
    src/telemetry/log_throttler.cpp
)

# Platform-specific sources
if(WIN32)
    list(APPEND AGENT_CORE_SOURCES 
        src/service/service_host_win.cpp
        src/service/service_installer_win.cpp
    )
else()
    list(APPEND AGENT_CORE_SOURCES 
        src/service/service_host_linux.cpp
        src/service/service_installer_linux.cpp
    )
endif()

# Main executable
add_executable(agent-core ${AGENT_CORE_SOURCES})

# Link libraries
target_link_libraries(agent-core PRIVATE CURL::libcurl)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(agent-core PRIVATE ws2_32)
else()
    target_link_libraries(agent-core PRIVATE pthread)
endif()

# ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(agent-core PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(agent-core PRIVATE ${ZMQ_LIBRARIES})
    target_compile_definitions(agent-core PRIVATE HAVE_ZMQ)
endif()

# Installation
install(TARGETS agent-core DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

# Optional: Tests subdirectory
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Agent Core Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
