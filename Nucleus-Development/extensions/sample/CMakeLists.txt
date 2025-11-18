cmake_minimum_required(VERSION 3.15)
project(sample-extension VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sample-ext main.cpp)

if(UNIX)
    target_link_libraries(sample-ext PRIVATE pthread)
endif()

install(TARGETS sample-ext DESTINATION bin)
