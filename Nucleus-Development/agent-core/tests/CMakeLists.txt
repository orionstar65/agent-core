# Unit and Integration tests

# Add cmake modules to path for ZeroMQ detection
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(FindZeroMQ)

# Collect all source files needed for integration tests (excluding main.cpp)
set(AGENT_LIB_SOURCES
    ../src/config/config_json.cpp
    ../src/identity/identity.cpp
    ../src/net/net_path_selector.cpp
    ../src/net/https_client.cpp
    ../src/net/comm_info_manager.cpp
    ../src/auth/auth_manager.cpp
    ../src/auth/aws_sigv4.cpp
    ../src/reg/registration_ssm.cpp
    ../src/mqtt/mqtt_client.cpp
    ../src/bus/zmq_bus.cpp
    ../src/bus/envelope_serialization.cpp
    ../src/ext/extension_manager.cpp
    ../src/ext/extension_manifest.cpp
    ../src/res/resource_monitor.cpp
    ../src/service/restart_manager.cpp
    ../src/service/restart_state_store.cpp
    ../src/util/retry.cpp
    ../src/util/uuid.cpp
    ../src/telemetry/logging.cpp
    ../src/telemetry/metrics.cpp
    ../src/telemetry/log_throttler.cpp
)

# Platform-specific sources
if(WIN32)
    list(APPEND AGENT_LIB_SOURCES 
        ../src/service/service_host_win.cpp
        ../src/service/service_installer_win.cpp
    )
else()
    list(APPEND AGENT_LIB_SOURCES 
        ../src/service/service_host_linux.cpp
        ../src/service/service_installer_linux.cpp
    )
endif()

# Find Paho MQTT libraries (same as main CMakeLists.txt)
find_library(PAHO_MQTT_C_LIB NAMES libpaho-mqtt3as.a paho-mqtt3as
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
find_library(PAHO_MQTT_CPP_LIB NAMES paho-mqttpp3 libpaho-mqttpp3.a
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
find_path(PAHO_MQTT_INCLUDE_DIR mqtt/async_client.h
    PATHS /usr/include /usr/local/include)

if(PAHO_MQTT_C_LIB AND PAHO_MQTT_CPP_LIB AND PAHO_MQTT_INCLUDE_DIR)
    set(PAHO_MQTT_FOUND TRUE)
    message(STATUS "Tests: Paho MQTT found")
else()
    set(PAHO_MQTT_FOUND FALSE)
    message(WARNING "Tests: Paho MQTT not found - MQTT tests will use stub")
endif()

# Helper function to setup test executable with all dependencies
function(setup_test_executable TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${TARGET_NAME} PRIVATE CURL::libcurl)
    
    # Paho MQTT
    if(PAHO_MQTT_FOUND)
        target_include_directories(${TARGET_NAME} PRIVATE ${PAHO_MQTT_INCLUDE_DIR})
        target_link_libraries(${TARGET_NAME} PRIVATE ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB})
        target_compile_definitions(${TARGET_NAME} PRIVATE HAVE_PAHO_MQTT)
    endif()
    
    # OpenSSL (required by Paho MQTT)
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        target_link_libraries(${TARGET_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    
    # ZeroMQ
    if(ZMQ_FOUND)
        target_include_directories(${TARGET_NAME} PRIVATE ${ZMQ_INCLUDE_DIRS})
        target_link_libraries(${TARGET_NAME} PRIVATE ${ZMQ_LIBRARIES})
        target_compile_definitions(${TARGET_NAME} PRIVATE HAVE_ZMQ)
        if(CPPZMQ_INCLUDE_DIR)
            target_include_directories(${TARGET_NAME} PRIVATE ${CPPZMQ_INCLUDE_DIR})
        endif()
    endif()
    
    # Platform-specific
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE ws2_32)
    else()
        target_link_libraries(${TARGET_NAME} PRIVATE pthread)
    endif()
endfunction()

# Integration test for authentication
add_executable(test_auth
    integration/test_auth.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_auth)

# Integration test for ZeroMQ
add_executable(test_zmq
    integration/test_zmq.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_zmq)

# ZeroMQ load test
add_executable(test_zmq_load
    integration/test_zmq_load.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_zmq_load)

# Integration test for SSM Registration
add_executable(test_ssm_registration
    integration/test_ssm_registration.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_ssm_registration)

# Identity discovery test
add_executable(test_identity
    integration/test_identity.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_identity)

# Unit test for RestartManager
add_executable(test_restart_manager
    unit/test_restart_manager.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_restart_manager)

# Unit test for Structured Logging
add_executable(test_logging
    unit/test_logging.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_logging)

# Unit test for Log Throttler
add_executable(test_log_throttler
    unit/test_log_throttler.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_log_throttler)

# Unit test for Retry Metrics
add_executable(test_retry_metrics
    unit/test_retry_metrics.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_retry_metrics)

# Integration test for Restart/Quarantine
add_executable(test_restart_quarantine
    integration/test_restart_quarantine.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_restart_quarantine)

# Integration test for Service Installer
add_executable(test_service_installer
integration/test_service_installer.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_service_installer)

# Integration test for Logging & Throttling
add_executable(test_logging_throttling
    integration/test_logging_throttling.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_logging_throttling)

# Unit test for ExtensionManager
add_executable(test_extension_manager
    unit/test_extension_manager.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_extension_manager)

# Integration test for Extension Lifecycle
add_executable(test_extension_lifecycle
    integration/test_extension_lifecycle.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_extension_lifecycle)

# Unit test for Health Response Format
add_executable(test_health_format
    unit/test_health_format.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_health_format)

# Unit test for Communication Info
add_executable(test_comm_info
    unit/test_comm_info.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_comm_info)

# Integration test for MQTT Connection
add_executable(test_mqtt_connection
    integration/test_mqtt_connection.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_mqtt_connection)

# Integration test for MQTT to ZeroMQ
add_executable(test_mqtt_zeromq
    integration/test_mqtt_zeromq.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_mqtt_zeromq)

# Register tests with CTest and set working directory
add_test(NAME RestartManagerUnitTest COMMAND test_restart_manager WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME LoggingUnitTest COMMAND test_logging WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME LogThrottlerUnitTest COMMAND test_log_throttler WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME RetryMetricsUnitTest COMMAND test_retry_metrics WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME RestartQuarantineIntegrationTest COMMAND test_restart_quarantine WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME AuthenticationIntegrationTest COMMAND test_auth WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME IdentityDiscoveryTest COMMAND test_identity WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ZeroMQIntegrationTest COMMAND test_zmq WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME LoggingThrottlingIntegrationTest COMMAND test_logging_throttling WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ZeroMQLoadTest COMMAND test_zmq_load WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ServiceInstallerTest COMMAND test_service_installer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ExtensionManagerUnitTest COMMAND test_extension_manager WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ExtensionLifecycleIntegrationTest COMMAND test_extension_lifecycle WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME HealthFormatUnitTest COMMAND test_health_format WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME CommInfoUnitTest COMMAND test_comm_info WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME MqttConnectionIntegrationTest COMMAND test_mqtt_connection --skip-network WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME MqttZeroMQIntegrationTest COMMAND test_mqtt_zeromq WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# Note: SSM registration and service installer tests require sudo for full testing
# Run manually with: sudo ./build/tests/test_ssm_registration --full
# Run manually with: sudo ./build/tests/test_service_installer --full
# Run manually with: ./build/tests/test_mqtt_connection --test-status-request

message(STATUS "Unit and integration tests configured")
