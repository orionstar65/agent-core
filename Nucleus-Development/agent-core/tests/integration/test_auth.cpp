#include "agent/auth_manager.hpp"
#include "agent/config.hpp"
#include "agent/identity.hpp"
#include <iostream>
#include <cassert>

using namespace agent;

// Test helper to create a test config
Config create_test_config() {
    Config config;
    config.backend.base_url = "https://35.159.104.91:443";
    config.backend.auth_path = "/deviceservices/api/Authentication/devicecertificatevalid/";
    config.cert.cert_path = "../../../cert_base64(200000).txt";
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
