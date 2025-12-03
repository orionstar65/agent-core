#include "agent/registration.hpp"
#include "agent/config.hpp"
#include "agent/identity.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#include <linux/limits.h>

using namespace agent;

// Get the absolute path to the agent-core directory
std::string get_agent_core_dir() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string path(exe_path);
        // Test executable is at: agent-core/build/tests/test_ssm_registration
        // We need to go up to agent-core directory
        size_t pos = path.rfind("/build/tests/");
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
    }
    // Fallback: try to get current working directory and navigate up
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return std::string(cwd);
    }
    return ".";
}

// Test helper to create a test config
Config create_test_config() {
    Config config;
    std::string agent_core_dir = get_agent_core_dir();
    
    config.backend.base_url = "https://35.159.104.91:443";
    config.backend.auth_path = "/deviceservices/api/Authentication/devicecertificatevalid/";
    config.backend.is_registered_path = "/deviceservices/api/devicemanagement/isdeviceregistered/";
    config.backend.get_activation_path = "/deviceservices/api/devicemanagement/getactivationinformation/";
    config.cert.cert_path = agent_core_dir + "/cert_base64(200000).txt";
    config.ssm.agent_path = "/snap/amazon-ssm-agent/current/amazon-ssm-agent";
    config.retry.max_attempts = 3;
    config.retry.base_ms = 500;
    config.retry.max_ms = 5000;
    
    std::cout << "Using certificate path: " << config.cert.cert_path << "\n";
    
    return config;
}

// Test helper to create a test identity
Identity create_test_identity() {
    Identity identity;
    identity.is_gateway = false;
    identity.device_serial = "200000";
    identity.uuid = "a1635025-2723-4ffa-b608-208578d6128f";
    return identity;
}

void test_check_backend_registration() {
    std::cout << "\n=== Test: Check Backend Registration Status ===\n";
    
    auto registration = create_ssm_registration();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    
    // This should succeed - just checking we can communicate with backend
    bool result = registration->is_device_registered(identity, config);
    
    // We don't assert the value since it depends on backend state,
    // but the call should not throw
    std::cout << "  - Backend registration status: " << (result ? "registered" : "not registered") << "\n";
    std::cout << "✓ Test passed: Backend registration check completed\n";
}

void test_check_local_registration() {
    std::cout << "\n=== Test: Check Local SSM Registration Status ===\n";
    
    auto registration = create_ssm_registration();
    
    // This should succeed - just checking we can query local SSM status
    bool result = registration->is_locally_registered();
    
    std::cout << "  - Local SSM agent status: " << (result ? "running" : "not running") << "\n";
    std::cout << "✓ Test passed: Local registration check completed\n";
}

void test_get_activation_info() {
    std::cout << "\n=== Test: Get Activation Info from Backend ===\n";
    
    auto registration = create_ssm_registration();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    
    ActivationInfo info;
    bool result = registration->get_activation_info(identity, config, info);
    
    assert(result && "Should successfully get activation info");
    assert(!info.activation_id.empty() && "Activation ID should not be empty");
    assert(!info.activation_code.empty() && "Activation code should not be empty");
    assert(!info.region.empty() && "Region should not be empty");
    
    std::cout << "  - Activation ID: " << info.activation_id << "\n";
    std::cout << "  - Region: " << info.region << "\n";
    std::cout << "  - Activation Code: [REDACTED - " << info.activation_code.length() << " chars]\n";
    std::cout << "✓ Test passed: Activation info retrieved successfully\n";
}

void test_get_activation_info_invalid_serial() {
    std::cout << "\n=== Test: Get Activation Info with Invalid Serial ===\n";
    
    auto registration = create_ssm_registration();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    identity.device_serial = "invalid-serial-99999";
    
    ActivationInfo info;
    bool result = registration->get_activation_info(identity, config, info);
    
    // Should fail or return empty info for invalid serial
    if (!result) {
        std::cout << "  - Correctly failed for invalid serial\n";
    } else {
        std::cout << "  - Backend returned info (may be test backend behavior)\n";
    }
    std::cout << "✓ Test passed: Invalid serial handled\n";
}

void test_get_activation_info_invalid_cert() {
    std::cout << "\n=== Test: Get Activation Info with Invalid Certificate ===\n";
    
    auto registration = create_ssm_registration();
    Config config = create_test_config();
    config.cert.cert_path = "/nonexistent/path/cert.txt";
    Identity identity = create_test_identity();
    
    ActivationInfo info;
    bool result = registration->get_activation_info(identity, config, info);
    
    assert(!result && "Should fail with invalid certificate path");
    std::cout << "✓ Test passed: Invalid certificate path handled correctly\n";
}

void test_register_with_ssm_invalid_info() {
    std::cout << "\n=== Test: Register with SSM using Invalid Info ===\n";
    
    auto registration = create_ssm_registration();
    
    ActivationInfo empty_info;
    // Empty activation info should fail
    
    RegistrationState result = registration->register_with_ssm(empty_info);
    
    assert(result == RegistrationState::Failed && "Should fail with empty activation info");
    std::cout << "✓ Test passed: Empty activation info handled correctly\n";
}

void test_full_registration_flow() {
    std::cout << "\n=== Test: Full Registration Flow ===\n";
    std::cout << "  NOTE: This test requires sudo privileges to register with SSM\n";
    
    auto registration = create_ssm_registration();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    
    RegistrationState result = registration->register_device(identity, config);
    
    if (result == RegistrationState::Registered) {
        std::cout << "  - Registration successful!\n";
    } else if (result == RegistrationState::Failed) {
        std::cout << "  - Registration failed (may need sudo privileges)\n";
    } else {
        std::cout << "  - Registration state: NotRegistered\n";
    }
    
    // We don't assert here since this depends on permissions and existing state
    std::cout << "✓ Test completed: Full registration flow executed\n";
}

void test_backend_url_invalid() {
    std::cout << "\n=== Test: Invalid Backend URL ===\n";
    
    auto registration = create_ssm_registration();
    Config config = create_test_config();
    config.backend.base_url = "https://invalid-nonexistent-host-12345.com:443";
    Identity identity = create_test_identity();
    
    bool result = registration->is_device_registered(identity, config);
    
    assert(!result && "Should fail with invalid backend URL");
    std::cout << "✓ Test passed: Invalid backend URL handled correctly (with retries)\n";
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "SSM Registration Integration Tests\n";
    std::cout << "========================================\n";
    
    bool run_full_registration = false;
    
    // Check for --full flag to run the full registration test
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--full") {
            run_full_registration = true;
            std::cout << "Running with --full flag: will execute full registration\n";
        }
    }
    
    try {
        // Tests that don't require sudo
        test_check_local_registration();
        test_check_backend_registration();
        test_get_activation_info();
        test_get_activation_info_invalid_serial();
        test_get_activation_info_invalid_cert();
        test_register_with_ssm_invalid_info();
        test_backend_url_invalid();
        
        // Full registration test (requires sudo)
        if (run_full_registration) {
            test_full_registration_flow();
        } else {
            std::cout << "\n=== Skipped: Full Registration Flow ===\n";
            std::cout << "  Run with --full flag to execute full registration\n";
            std::cout << "  Example: sudo ./test_ssm_registration --full\n";
        }
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}
