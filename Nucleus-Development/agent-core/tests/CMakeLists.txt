# Test executables
# Source files for agent-core library (without main.cpp)
set(AGENT_LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/config/config_json.cpp
    ${CMAKE_SOURCE_DIR}/src/identity/identity.cpp
    ${CMAKE_SOURCE_DIR}/src/net/net_path_selector.cpp
    ${CMAKE_SOURCE_DIR}/src/net/https_client.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/auth_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/reg/registration_ssm.cpp
    ${CMAKE_SOURCE_DIR}/src/mqtt/mqtt_client.cpp
    ${CMAKE_SOURCE_DIR}/src/bus/zmq_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/bus/envelope_serialization.cpp
    ${CMAKE_SOURCE_DIR}/src/ext/extension_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/res/resource_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/util/retry.cpp
    ${CMAKE_SOURCE_DIR}/src/util/uuid.cpp
    ${CMAKE_SOURCE_DIR}/src/util/path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/telemetry/logging.cpp
    ${CMAKE_SOURCE_DIR}/src/telemetry/metrics.cpp
)

# Platform-specific sources (exclude service_host for tests - they have dependencies on main.cpp)
# Tests don't need service host functionality

# Common test setup function
function(setup_test_executable target_name)
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${target_name} PRIVATE CURL::libcurl)
    
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32)
    else()
        target_link_libraries(${target_name} PRIVATE pthread)
    endif()
    
    # ZeroMQ and cppzmq
    if(ZMQ_FOUND_VIA_CONFIG)
        find_package(ZeroMQ CONFIG REQUIRED)
        target_link_libraries(${target_name} PRIVATE libzmq)
        target_compile_definitions(${target_name} PRIVATE HAVE_ZMQ)
        
        # cppzmq is header-only
        get_filename_component(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed" ABSOLUTE)
        target_include_directories(${target_name} PRIVATE "${VCPKG_INSTALLED_DIR}/x64-windows/include")
    elseif(ZMQ_FOUND)
        target_include_directories(${target_name} PRIVATE ${ZMQ_INCLUDE_DIRS})
        target_link_libraries(${target_name} PRIVATE ${ZMQ_LIBRARIES})
        target_compile_definitions(${target_name} PRIVATE HAVE_ZMQ)
    endif()
endfunction()

# ZeroMQ integration test
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

# Authentication integration test
add_executable(test_auth
    integration/test_auth.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_auth)

# SSM Registration integration test
add_executable(test_ssm_registration
    integration/test_ssm_registration.cpp
    ${AGENT_LIB_SOURCES}
)
setup_test_executable(test_ssm_registration)

# Register tests with CTest
add_test(NAME ZeroMQIntegrationTest COMMAND test_zmq WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME ZeroMQLoadTest COMMAND test_zmq_load WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME AuthenticationIntegrationTest COMMAND test_auth WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# Note: SSM registration test requires sudo on Linux, so it's optional
# add_test(NAME SSMRegistrationTest COMMAND test_ssm_registration WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
