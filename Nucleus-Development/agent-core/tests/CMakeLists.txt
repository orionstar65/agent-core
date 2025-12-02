# Integration tests

# Add cmake modules to path for ZeroMQ detection
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(FindZeroMQ)

# Collect all source files needed for integration tests (excluding main.cpp)
set(AGENT_LIB_SOURCES
    ../src/config/config_json.cpp
    ../src/identity/identity.cpp
    ../src/net/net_path_selector.cpp
    ../src/net/https_client.cpp
    ../src/auth/auth_manager.cpp
    ../src/reg/registration_ssm.cpp
    ../src/mqtt/mqtt_client.cpp
    ../src/bus/zmq_bus.cpp
    ../src/bus/envelope_serialization.cpp
    ../src/ext/extension_manager.cpp
    ../src/res/resource_monitor.cpp
    ../src/util/retry.cpp
    ../src/util/uuid.cpp
    ../src/telemetry/logging.cpp
    ../src/telemetry/metrics.cpp
)

# Platform-specific sources
if(WIN32)
    list(APPEND AGENT_LIB_SOURCES ../src/service/service_host_win.cpp)
else()
    list(APPEND AGENT_LIB_SOURCES ../src/service/service_host_linux.cpp)
endif()

# Integration test for authentication
add_executable(test_auth
    integration/test_auth.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_auth PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_auth PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_auth PRIVATE ws2_32)
else()
    target_link_libraries(test_auth PRIVATE pthread)
endif()

# Integration test for ZeroMQ
add_executable(test_zmq
    integration/test_zmq.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_zmq PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_zmq PRIVATE CURL::libcurl)

# ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(test_zmq PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(test_zmq PRIVATE ${ZMQ_LIBRARIES})
    target_compile_definitions(test_zmq PRIVATE HAVE_ZMQ)
endif()

if(WIN32)
    target_link_libraries(test_zmq PRIVATE ws2_32)
else()
    target_link_libraries(test_zmq PRIVATE pthread)
endif()

# Register tests with CTest and set working directory
add_test(NAME AuthenticationIntegrationTest COMMAND test_auth WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ZeroMQIntegrationTest COMMAND test_zmq WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

message(STATUS "Integration tests configured")
