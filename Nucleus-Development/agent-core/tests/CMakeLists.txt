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
    ../src/auth/auth_manager.cpp
    ../src/reg/registration_ssm.cpp
    ../src/mqtt/mqtt_client.cpp
    ../src/bus/zmq_bus.cpp
    ../src/bus/envelope_serialization.cpp
    ../src/ext/extension_manager.cpp
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

# Unit test for RestartManager
add_executable(test_restart_manager
    unit/test_restart_manager.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_restart_manager PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_restart_manager PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_restart_manager PRIVATE ws2_32)
else()
    target_link_libraries(test_restart_manager PRIVATE pthread)
endif()

# Unit test for Structured Logging
add_executable(test_logging
    unit/test_logging.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_logging PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_logging PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_logging PRIVATE ws2_32)
else()
    target_link_libraries(test_logging PRIVATE pthread)
endif()

# Unit test for Log Throttler
add_executable(test_log_throttler
    unit/test_log_throttler.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_log_throttler PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_log_throttler PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_log_throttler PRIVATE ws2_32)
else()
    target_link_libraries(test_log_throttler PRIVATE pthread)
endif()

# Unit test for Retry Metrics
add_executable(test_retry_metrics
    unit/test_retry_metrics.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_retry_metrics PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_retry_metrics PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_retry_metrics PRIVATE ws2_32)
else()
    target_link_libraries(test_retry_metrics PRIVATE pthread)
endif()

# Integration test for Restart/Quarantine
add_executable(test_restart_quarantine
    integration/test_restart_quarantine.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_restart_quarantine PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_restart_quarantine PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_restart_quarantine PRIVATE ws2_32)
else()
    target_link_libraries(test_restart_quarantine PRIVATE pthread)
endif()

# Integration test for Service Installer
add_executable(test_service_installer
integration/test_service_installer.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_service_installer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_service_installer PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_service_installer PRIVATE ws2_32)
else()
    target_link_libraries(test_service_installer PRIVATE pthread)
endif()

# Integration test for Logging & Throttling
add_executable(test_logging_throttling
    integration/test_logging_throttling.cpp
    ${AGENT_LIB_SOURCES}
)

target_include_directories(test_logging_throttling PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_logging_throttling PRIVATE CURL::libcurl)

if(WIN32)
    target_link_libraries(test_logging_throttling PRIVATE ws2_32)
else()
    target_link_libraries(test_logging_throttling PRIVATE pthread)
endif()

# Register tests with CTest and set working directory
add_test(NAME RestartManagerUnitTest COMMAND test_restart_manager WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME LoggingUnitTest COMMAND test_logging WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME LogThrottlerUnitTest COMMAND test_log_throttler WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME RetryMetricsUnitTest COMMAND test_retry_metrics WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME RestartQuarantineIntegrationTest COMMAND test_restart_quarantine WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME AuthenticationIntegrationTest COMMAND test_auth WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ZeroMQIntegrationTest COMMAND test_zmq WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME LoggingThrottlingIntegrationTest COMMAND test_logging_throttling WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ServiceInstallerTest COMMAND test_service_installer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# Note: SSM registration and service installer tests require sudo for full testing
# Run manually with: sudo ./build/tests/test_ssm_registration --full
# Run manually with: sudo ./build/tests/test_service_installer --full

message(STATUS "Unit and integration tests configured")
