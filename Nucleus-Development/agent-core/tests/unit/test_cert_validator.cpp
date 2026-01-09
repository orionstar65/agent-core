#include "agent/cert_validator.hpp"
#include <iostream>
#include <cassert>
#include <fstream>
#include <sstream>

using namespace agent;

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void test_empty_certificate() {
    std::cout << "\n=== Test: Empty Certificate ===\n";
    
    CertValidationResult result = validate_certificate("");
    assert(!result.valid);
    assert(result.error == CertValidationError::InvalidFormat);
    assert(!result.error_message.empty());
    
    std::cout << "✓ Empty certificate correctly rejected\n";
}

void test_malformed_certificate() {
    std::cout << "\n=== Test: Malformed Certificate ===\n";
    
    std::string malformed = "-----BEGIN CERTIFICATE-----\n"
                           "This is not a valid certificate\n"
                           "-----END CERTIFICATE-----";
    
    CertValidationResult result = validate_certificate(malformed);
    assert(!result.valid);
    assert(result.error == CertValidationError::InvalidFormat);
    assert(!result.error_message.empty());
    
    std::cout << "✓ Malformed certificate correctly rejected\n";
}

void test_valid_certificate() {
    std::cout << "\n=== Test: Valid Certificate ===\n";
    
    std::string cert_path = "tests/fixtures/certs/valid.pem";
    std::string valid_cert = read_file(cert_path);
    
    if (valid_cert.empty()) {
        std::cout << "  Skipping: Certificate file not found at " << cert_path << "\n";
        std::cout << "  (This is expected if test certificates haven't been generated)\n";
        return;
    }
    
    CertValidationResult result = validate_certificate(valid_cert);
    assert(result.valid);
    assert(result.error == CertValidationError::None);
    
    std::cout << "✓ Valid certificate accepted\n";
}

void test_certificate_from_file() {
    std::cout << "\n=== Test: Certificate from File ===\n";
    
    std::string cert_path = "tests/fixtures/certs/valid.pem";
    std::string cert_content = read_file(cert_path);
    
    if (cert_content.empty()) {
        std::cout << "  Skipping: Certificate file not found at " << cert_path << "\n";
        std::cout << "  (This is expected if test certificates haven't been generated)\n";
        return;
    }
    
    CertValidationResult result = validate_certificate(cert_content);
    assert(result.valid);
    assert(result.error == CertValidationError::None);
    
    std::cout << "✓ Certificate from file validated successfully\n";
}

void test_expired_certificate() {
    std::cout << "\n=== Test: Expired Certificate ===\n";
    
    std::string cert_path = "tests/fixtures/certs/expired.pem";
    std::string cert_content = read_file(cert_path);
    
    if (cert_content.empty()) {
        std::cout << "  Skipping: Expired certificate file not found at " << cert_path << "\n";
        std::cout << "  (This is expected if test certificates haven't been generated)\n";
        return;
    }
    
    CertValidationResult result = validate_certificate(cert_content);
    assert(!result.valid);
    assert(result.error == CertValidationError::Expired);
    assert(!result.error_message.empty());
    
    std::cout << "✓ Expired certificate correctly rejected\n";
}

int main() {
    std::cout << "=== Certificate Validator Unit Tests ===\n";
    
    test_empty_certificate();
    test_malformed_certificate();
    test_valid_certificate();
    test_certificate_from_file();
    test_expired_certificate();
    
    std::cout << "\n=== All Tests Completed ===\n";
    return 0;
}

