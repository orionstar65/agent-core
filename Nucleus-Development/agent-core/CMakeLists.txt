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
    # Use static runtime to avoid requiring VCRUNTIME DLLs
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # NOTE: Windows builds use MSVC only (not MSYS2/MinGW)
    # Defensive filtering below prevents accidental use of MSYS2/MinGW paths if installed on system
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Windows builds use MSVC only (via Visual Studio)
# Defensive filtering: Exclude MSYS2/MinGW paths if accidentally present in environment
if(MSVC)
    # Remove MSYS2/MinGW paths from CMAKE_PREFIX_PATH (defensive, should not be needed)
    if(CMAKE_PREFIX_PATH)
        list(FILTER CMAKE_PREFIX_PATH EXCLUDE REGEX ".*[Mm][Ss][Yy][Ss].*")
        list(FILTER CMAKE_PREFIX_PATH EXCLUDE REGEX ".*[Mm][Ii][Nn][Gg][Ww].*")
    endif()
    
    # Environment variable cleaning is done in build-msvc.ps1 script
    # This ensures MSYS2/MinGW paths are removed from PATH/INCLUDE before CMake runs
endif()

# Find required packages
# For Windows MSVC builds, use vcpkg for dependencies (CURL, ZeroMQ)
# Set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake if using vcpkg
find_package(CURL REQUIRED)

# Defensive filtering: Exclude MSYS2/MinGW paths from CURL (should not be needed with vcpkg)
if(MSVC AND CURL_INCLUDE_DIRS)
    list(FILTER CURL_INCLUDE_DIRS EXCLUDE REGEX ".*[Mm][Ss][Yy][Ss].*")
    list(FILTER CURL_INCLUDE_DIRS EXCLUDE REGEX ".*[Mm][Ii][Nn][Gg][Ww].*")
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
    src/auth/auth_manager.cpp
    src/reg/registration_ssm.cpp
    src/mqtt/mqtt_client.cpp
    src/bus/zmq_bus.cpp
    src/bus/envelope_serialization.cpp
    src/ext/extension_manager.cpp
    src/res/resource_monitor.cpp
    src/util/retry.cpp
    src/util/uuid.cpp
    src/util/path_utils.cpp
    src/telemetry/logging.cpp
    src/telemetry/metrics.cpp
)

# Platform-specific sources
if(WIN32)
    list(APPEND AGENT_CORE_SOURCES src/service/service_host_win.cpp)
else()
    list(APPEND AGENT_CORE_SOURCES src/service/service_host_linux.cpp)
endif()

# Main executable
add_executable(agent-core ${AGENT_CORE_SOURCES})

# Defensive filtering: Exclude MSYS2/MinGW include directories (should not be needed)
if(MSVC)
    # Get current include directories and filter out MSYS2/MinGW paths
    get_target_property(currentIncludes agent-core INCLUDE_DIRECTORIES)
    if(currentIncludes)
        list(FILTER currentIncludes EXCLUDE REGEX ".*[Mm][Ss][Yy][Ss].*")
        list(FILTER currentIncludes EXCLUDE REGEX ".*[Mm][Ii][Nn][Gg][Ww].*")
        set_target_properties(agent-core PROPERTIES INCLUDE_DIRECTORIES "${currentIncludes}")
    endif()
    
    # Also filter system include directories
    get_target_property(currentSystemIncludes agent-core INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    if(currentSystemIncludes)
        list(FILTER currentSystemIncludes EXCLUDE REGEX ".*[Mm][Ss][Yy][Ss].*")
        list(FILTER currentSystemIncludes EXCLUDE REGEX ".*[Mm][Ii][Nn][Gg][Ww].*")
        set_target_properties(agent-core PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${currentSystemIncludes}")
    endif()
endif()

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
