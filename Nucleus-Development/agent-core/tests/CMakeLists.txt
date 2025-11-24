find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(test_bus
    unit/test_bus.cpp
    ../src/bus/envelope_serialization.cpp
)

target_link_libraries(test_bus PRIVATE GTest::gtest GTest::gtest_main GTest::gmock)
target_include_directories(test_bus PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME BusTests COMMAND test_bus)

message(STATUS "Bus tests configured")
