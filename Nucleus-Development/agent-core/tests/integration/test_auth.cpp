#include "agent/auth_manager.hpp"
#include "agent/config.hpp"
#include "agent/identity.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <limits.h>
#else
#include <linux/limits.h>
#endif

using namespace agent;

// Get the absolute path to the agent-core directory
std::string get_agent_core_dir() {
    char exe_path[PATH_MAX];
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, exe_path, sizeof(exe_path));
    if (len == 0 || len >= sizeof(exe_path)) {
        return "";
    }
    exe_path[len] = '\0';
    std::string path(exe_path);
    // Test executable is at: agent-core/build/tests/test_auth.exe
    // We need to go up to agent-core directory
    size_t pos = path.rfind("\\build\\tests\\");
    if (pos == std::string::npos) {
        pos = path.rfind("/build/tests/");
    }
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string path(exe_path);
        // Test executable is at: agent-core/build/tests/test_auth
        // We need to go up to agent-core directory
        size_t pos = path.rfind("/build/tests/");
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
    }
#endif
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
    config.cert.cert_path = agent_core_dir + "/cert_base64(200000).txt";
    config.retry.max_attempts = 3;
    config.retry.base_ms = 500;
    config.retry.max_ms = 5000;
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

void test_successful_authentication() {
    std::cout << "\n=== Test: Successful Authentication ===\n";
    
    auto auth_manager = create_auth_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    
    CertState result = auth_manager->ensure_certificate(identity, config);
    
    assert(result == CertState::Valid && "Authentication should succeed");
    std::cout << "✓ Test passed: Successful authentication\n";
}

void test_missing_serial_number() {
    std::cout << "\n=== Test: Missing Serial Number ===\n";
    
    auto auth_manager = create_auth_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    identity.device_serial = "";  // Empty serial number
    
    CertState result = auth_manager->ensure_certificate(identity, config);
    
    assert(result == CertState::Failed && "Authentication should fail with empty serial");
    std::cout << "✓ Test passed: Missing serial number handled correctly\n";
}

void test_missing_uuid() {
    std::cout << "\n=== Test: Missing UUID ===\n";
    
    auto auth_manager = create_auth_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    identity.uuid = "";  // Empty UUID
    
    CertState result = auth_manager->ensure_certificate(identity, config);
    
    assert(result == CertState::Failed && "Authentication should fail with empty UUID");
    std::cout << "✓ Test passed: Missing UUID handled correctly\n";
}

void test_invalid_cert_path() {
    std::cout << "\n=== Test: Invalid Certificate Path ===\n";
    
    auto auth_manager = create_auth_manager();
    Config config = create_test_config();
    config.cert.cert_path = "/nonexistent/path/cert.txt";
    Identity identity = create_test_identity();
    
    CertState result = auth_manager->ensure_certificate(identity, config);
    
    assert(result == CertState::Failed && "Authentication should fail with invalid cert path");
    std::cout << "✓ Test passed: Invalid certificate path handled correctly\n";
}

void test_invalid_backend_url() {
    std::cout << "\n=== Test: Invalid Backend URL ===\n";
    
    auto auth_manager = create_auth_manager();
    Config config = create_test_config();
    config.backend.base_url = "https://invalid-nonexistent-host-12345.com:443";
    Identity identity = create_test_identity();
    
    CertState result = auth_manager->ensure_certificate(identity, config);
    
    assert(result == CertState::Failed && "Authentication should fail with invalid URL");
    std::cout << "✓ Test passed: Invalid backend URL handled correctly (with retries)\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Authentication Integration Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_successful_authentication();
        test_missing_serial_number();
        test_missing_uuid();
        test_invalid_cert_path();
        test_invalid_backend_url();
        
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
