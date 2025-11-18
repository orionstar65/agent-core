cmake_minimum_required(VERSION 3.15)
project(tunnel-extension VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
add_executable(ext-tunnel 
    src/main.cpp
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(ext-tunnel PRIVATE ws2_32)
else()
    target_link_libraries(ext-tunnel PRIVATE pthread)
endif()

# TODO: Add ZeroMQ dependency when available
# find_package(ZeroMQ REQUIRED)
# target_link_libraries(ext-tunnel PRIVATE ZeroMQ::ZeroMQ)

install(TARGETS ext-tunnel DESTINATION bin)

message(STATUS "Tunnel Extension configured")
