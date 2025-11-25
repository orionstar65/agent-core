# Integration tests

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

# Register test with CTest
add_test(NAME AuthenticationIntegrationTest COMMAND test_auth)

message(STATUS "Integration tests configured")
