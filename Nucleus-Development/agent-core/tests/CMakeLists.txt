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
    
    # Windows builds use MSVC only - defensive filtering for MSYS2/MinGW paths
    # This prevents MSVC from picking up GCC-specific headers
    # We use /X to ignore standard include paths from INCLUDE env var, but that's too aggressive
    # Instead, we'll filter include directories at the target level
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# For MSVC builds on Windows, configure package finding
if(MSVC)
    # If vcpkg toolchain is set, prefer vcpkg packages
    if(CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
        message(STATUS "Using vcpkg for package management")
    else()
        set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
        message(WARNING "vcpkg not detected. Consider using vcpkg for better dependency management.")
        message(WARNING "If you see errors about OpenSSL, install vcpkg or use the build script: scripts/build-msvc.ps1")
    endif()
    
    # Note: Environment variable cleaning is done in build-msvc.ps1 script
    # This ensures MSYS2/MinGW paths are removed from PATH/INCLUDE before CMake runs
endif()

# Find CURL - will use vcpkg if CMAKE_TOOLCHAIN_FILE is set
# Otherwise will search system paths (but MSYS2 paths should be excluded)
find_package(CURL REQUIRED)

# Defensive filtering: Exclude MSYS2/MinGW paths from CURL (should not be needed with vcpkg)
if(MSVC)
    # Windows builds use MSVC only - check if CURL was found in MSYS2/MinGW path (should not happen)
    if(CURL_INCLUDE_DIRS)
        foreach(inc_dir ${CURL_INCLUDE_DIRS})
            if(inc_dir MATCHES ".*[Mm][Ss][Yy][Ss].*" OR inc_dir MATCHES ".*[Mm][Ii][Nn][Gg][Ww].*")
                message(FATAL_ERROR "ERROR: CURL was found in MSYS2/MinGW path: ${inc_dir}. Windows builds must use MSVC with vcpkg. Use the build script (scripts/build-msvc.ps1) which sets up vcpkg automatically.")
            endif()
        endforeach()
    endif()
    
    # Defensive filtering: Remove MSYS2/MinGW paths (should not be needed)
    if(CURL_INCLUDE_DIRS)
        list(FILTER CURL_INCLUDE_DIRS EXCLUDE REGEX ".*[Mm][Ss][Yy][Ss].*")
        list(FILTER CURL_INCLUDE_DIRS EXCLUDE REGEX ".*[Mm][Ii][Nn][Gg][Ww].*")
    endif()
    if(CURL_LIBRARIES)
        list(FILTER CURL_LIBRARIES EXCLUDE REGEX ".*[Mm][Ss][Yy][Ss].*")
        list(FILTER CURL_LIBRARIES EXCLUDE REGEX ".*[Mm][Ii][Nn][Gg][Ww].*")
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

# Integration test for SSM Registration
add_executable(test_ssm_registration
    integration/test_ssm_registration.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_ssm_registration PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_ssm_registration PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_ssm_registration PRIVATE ws2_32)
else()
    target_link_libraries(test_ssm_registration PRIVATE pthread)
endif()

# Register tests with CTest and set working directory
add_test(NAME AuthenticationIntegrationTest COMMAND test_auth WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ZeroMQIntegrationTest COMMAND test_zmq WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# Note: SSM registration test requires sudo, so it's not added to CTest by default
# Run manually with: sudo ./build/tests/test_ssm_registration --full

message(STATUS "Integration tests configured")
