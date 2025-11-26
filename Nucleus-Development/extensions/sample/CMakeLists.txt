cmake_minimum_required(VERSION 3.15)
project(sample-extension VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add cmake modules to path (point to agent-core cmake directory)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../agent-core/cmake)

# Find ZeroMQ
include(FindZeroMQ)

add_executable(sample-ext main.cpp)

target_include_directories(sample-ext PRIVATE ../../agent-core/include)

if(UNIX)
    target_link_libraries(sample-ext PRIVATE pthread)
endif()

# ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(sample-ext PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(sample-ext PRIVATE ${ZMQ_LIBRARIES})
    target_compile_definitions(sample-ext PRIVATE HAVE_ZMQ)
endif()

install(TARGETS sample-ext DESTINATION bin)
