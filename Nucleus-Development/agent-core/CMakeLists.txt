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

# Option for static linking (preferred for IoT deployment)
# NOTE: Paho MQTT C++ static library not available in Ubuntu packages
#       See TODO_STATIC_LINKING.md for instructions to build from source
option(BUILD_STATIC "Build with static linking where possible" ON)

if(BUILD_STATIC)
    message(STATUS "Configuring for static linking (where available)")
    if(WIN32)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        # Linux: Prefer static but allow dynamic for Paho MQTT C++
        message(STATUS "Note: Paho MQTT C++ will use dynamic linking until static lib is built")
    endif()
else()
    message(STATUS "Configuring for dynamic linking")
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find required packages - ALL STATIC ONLY

# CURL - static
set(CURL_STATICLIB ON)
find_package(CURL REQUIRED)

# Paho MQTT C - prefer /usr/local (newer with WebSocket support)
# On Windows/msys64, also check /ucrt64 paths
if(WIN32)
    set(PAHO_SEARCH_PATHS_LIB 
        /ucrt64/lib 
        C:/msys64/ucrt64/lib
        /usr/local/lib 
        /usr/lib 
        /usr/lib/x86_64-linux-gnu)
    set(PAHO_SEARCH_PATHS_INCLUDE 
        /ucrt64/include 
        C:/msys64/ucrt64/include
        /usr/local/include 
        /usr/include)
else()
    set(PAHO_SEARCH_PATHS_LIB 
        /usr/local/lib 
        /usr/lib 
        /usr/lib/x86_64-linux-gnu)
    set(PAHO_SEARCH_PATHS_INCLUDE 
        /usr/local/include 
        /usr/include)
endif()

find_library(PAHO_MQTT_C_LIB NAMES paho-mqtt3as libpaho-mqtt3as.a paho-mqtt3as.dll.a
    PATHS ${PAHO_SEARCH_PATHS_LIB}
    NO_DEFAULT_PATH)

# Paho MQTT C++ - check /usr/local first, then system
find_library(PAHO_MQTT_CPP_LIB NAMES paho-mqttpp3 libpaho-mqttpp3.a libpaho-mqttpp3-static.a paho-mqttpp3.dll.a
    PATHS ${PAHO_SEARCH_PATHS_LIB})

find_path(PAHO_MQTT_INCLUDE_DIR mqtt/async_client.h
    PATHS ${PAHO_SEARCH_PATHS_INCLUDE})

if(PAHO_MQTT_C_LIB AND PAHO_MQTT_CPP_LIB AND PAHO_MQTT_INCLUDE_DIR)
    set(PAHO_MQTT_FOUND TRUE)
    message(STATUS "Paho MQTT C: ${PAHO_MQTT_C_LIB}")
    message(STATUS "Paho MQTT C++: ${PAHO_MQTT_CPP_LIB}")
    message(STATUS "Paho MQTT headers: ${PAHO_MQTT_INCLUDE_DIR}")
    
    # Check if using dynamic library and warn
    if(PAHO_MQTT_CPP_LIB MATCHES "\\.so")
        message(WARNING "Using dynamic Paho MQTT C++ library - see TODO_STATIC_LINKING.md")
    endif()
else()
    set(PAHO_MQTT_FOUND FALSE)
    if(WIN32)
        message(FATAL_ERROR "Paho MQTT libraries not found! Install via pacman: pacman -S mingw-w64-ucrt-x86_64-paho.mqtt.c, or build from source")
    else()
        message(FATAL_ERROR "Paho MQTT libraries not found! Install: sudo apt-get install libpaho-mqtt-dev libpaho-mqttpp-dev")
    endif()
endif()

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
    src/net/comm_info_manager.cpp
    src/auth/auth_manager.cpp
    src/auth/aws_sigv4.cpp
    src/reg/registration_ssm.cpp
    src/mqtt/mqtt_client.cpp
    src/bus/zmq_bus.cpp
    src/bus/envelope_serialization.cpp
    src/ext/extension_manager.cpp
    src/ext/extension_manifest.cpp
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

# Paho MQTT
if(PAHO_MQTT_FOUND)
    target_include_directories(agent-core PRIVATE ${PAHO_MQTT_INCLUDE_DIR})
    target_link_libraries(agent-core PRIVATE ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB})
    target_compile_definitions(agent-core PRIVATE HAVE_PAHO_MQTT)
endif()

# Platform-specific libraries - ALL STATIC
if(WIN32)
    target_link_libraries(agent-core PRIVATE ws2_32 crypt32 wldap32 normaliz bcrypt)
    
    # OpenSSL (required by CURL, Paho MQTT, and AWS SigV4)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(agent-core PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
else()
    # Linux system libraries
    target_link_libraries(agent-core PRIVATE pthread rt dl m)
    
    # OpenSSL (required by CURL and Paho MQTT)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(agent-core PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
endif()

# ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(agent-core PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(agent-core PRIVATE ${ZMQ_LIBRARIES})
    target_compile_definitions(agent-core PRIVATE HAVE_ZMQ)
    
    # Find cppzmq headers (C++ bindings for ZeroMQ)
    # On msys64, cppzmq headers are typically in /ucrt64/include
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND EXISTS "C:/msys64/ucrt64/include/zmq.hpp")
        target_include_directories(agent-core PRIVATE "C:/msys64/ucrt64/include")
    elseif(EXISTS "${CMAKE_INSTALL_PREFIX}/include/zmq.hpp")
        target_include_directories(agent-core PRIVATE "${CMAKE_INSTALL_PREFIX}/include")
    else()
        # Try to find cppzmq in common locations
        find_path(CPPZMQ_INCLUDE_DIR zmq.hpp
            PATHS
                /usr/include
                /usr/local/include
                ${CMAKE_INSTALL_PREFIX}/include
            NO_DEFAULT_PATH
        )
        if(CPPZMQ_INCLUDE_DIR)
            target_include_directories(agent-core PRIVATE ${CPPZMQ_INCLUDE_DIR})
        endif()
    endif()
endif()

# Health query tool
set(HEALTH_TOOL_SOURCES
    tools/health_query.cpp
    src/bus/zmq_bus.cpp
    src/bus/envelope_serialization.cpp
    src/util/uuid.cpp
    src/telemetry/logging.cpp
    src/telemetry/log_throttler.cpp
    src/telemetry/metrics.cpp
)

add_executable(agent-health-query ${HEALTH_TOOL_SOURCES})
target_link_libraries(agent-health-query PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(agent-health-query PRIVATE ws2_32)
else()
    target_link_libraries(agent-health-query PRIVATE pthread)
endif()

if(ZMQ_FOUND)
    target_include_directories(agent-health-query PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(agent-health-query PRIVATE ${ZMQ_LIBRARIES})
    target_compile_definitions(agent-health-query PRIVATE HAVE_ZMQ)
endif()

# Installation
install(TARGETS agent-core agent-health-query DESTINATION bin)
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
message(STATUS "  Static linking: ${BUILD_STATIC}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  ZeroMQ found: ${ZMQ_FOUND}")
message(STATUS "  Paho MQTT found: ${PAHO_MQTT_FOUND}")
message(STATUS "")
