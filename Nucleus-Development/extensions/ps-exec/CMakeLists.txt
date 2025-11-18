cmake_minimum_required(VERSION 3.15)
project(ps-exec-extension VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
add_executable(ext-ps 
    src/main.cpp
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(ext-ps PRIVATE ws2_32)
else()
    target_link_libraries(ext-ps PRIVATE pthread)
endif()

# TODO: Add ZeroMQ dependency when available
# find_package(ZeroMQ REQUIRED)
# target_link_libraries(ext-ps PRIVATE ZeroMQ::ZeroMQ)

install(TARGETS ext-ps DESTINATION bin)

message(STATUS "PowerShell Execution Extension configured")
