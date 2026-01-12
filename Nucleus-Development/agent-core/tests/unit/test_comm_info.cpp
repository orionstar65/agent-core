#include "agent/comm_info.hpp"
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
        size_t pos = path.rfind("/build/tests/");
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
    }
#endif
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
    config.backend.comm_info_path = "/deviceservices/api/DeviceManagement/getcommunicationinformation/";
    config.backend.use_persistent_session = true;  // Use AWS IoT Core endpoint
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

void test_successful_comm_info_fetch() {
    std::cout << "\n=== Test: Successful Communication Info Fetch ===\n";
    
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info;
    
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    
    assert(result && "Fetching comm info should succeed");
    assert(!comm_info.endpoint.empty() && "Endpoint should not be empty");
    assert(!comm_info.access_key.empty() && "Access key should not be empty");
    assert(!comm_info.secret_key.empty() && "Secret key should not be empty");
    assert(!comm_info.token.empty() && "Token should not be empty");
    assert(comm_info.is_valid() && "Communication info should be valid");
    
    std::cout << "  ✓ Endpoint: " << comm_info.endpoint << "\n";
    std::cout << "  ✓ Region: " << comm_info.region << "\n";
    std::cout << "  ✓ Access Key: " << comm_info.access_key.substr(0, 4) << "..." << "\n";
    std::cout << "✓ Test passed: Successful communication info fetch\n";
}

void test_missing_serial_number() {
    std::cout << "\n=== Test: Missing Serial Number ===\n";
    
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    identity.device_serial = "";  // Empty serial number
    CommunicationInfo comm_info;
    
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    
    assert(!result && "Fetching comm info should fail with empty serial");
    std::cout << "✓ Test passed: Missing serial number handled correctly\n";
}

void test_missing_uuid() {
    std::cout << "\n=== Test: Missing UUID ===\n";
    
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    identity.uuid = "";  // Empty UUID
    CommunicationInfo comm_info;
    
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    
    assert(!result && "Fetching comm info should fail with empty UUID");
    std::cout << "✓ Test passed: Missing UUID handled correctly\n";
}

void test_invalid_cert_path() {
    std::cout << "\n=== Test: Invalid Certificate Path ===\n";
    
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    config.cert.cert_path = "/nonexistent/path/cert.txt";
    Identity identity = create_test_identity();
    CommunicationInfo comm_info;
    
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    
    assert(!result && "Fetching comm info should fail with invalid cert path");
    std::cout << "✓ Test passed: Invalid certificate path handled correctly\n";
}

void test_invalid_backend_url() {
    std::cout << "\n=== Test: Invalid Backend URL ===\n";
    
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    config.backend.base_url = "https://invalid-nonexistent-host-12345.com:443";
    Identity identity = create_test_identity();
    CommunicationInfo comm_info;
    
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    
    assert(!result && "Fetching comm info should fail with invalid URL");
    std::cout << "✓ Test passed: Invalid backend URL handled correctly (with retries)\n";
}

void test_communication_info_validity() {
    std::cout << "\n=== Test: Communication Info Validity ===\n";
    
    CommunicationInfo comm_info;
    assert(!comm_info.is_valid() && "Empty comm info should be invalid");
    
    comm_info.endpoint = "test.iot.aws.com";
    assert(!comm_info.is_valid() && "Comm info with only endpoint should be invalid");
    
    comm_info.access_key = "AKIATEST123";
    assert(!comm_info.is_valid() && "Comm info without secret_key should be invalid");
    
    comm_info.secret_key = "secret123";
    assert(!comm_info.is_valid() && "Comm info without token should be invalid");
    
    comm_info.token = "token123";
    assert(comm_info.is_valid() && "Comm info with all required fields should be valid");
    
    std::cout << "✓ Test passed: Communication info validity check works correctly\n";
}

void test_persistent_session_flag() {
    std::cout << "\n=== Test: Persistent Session Flag ===\n";
    
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info;
    
    // Test with use_persistent_session = false (default for comm_info)
    config.backend.use_persistent_session = false;
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    assert(result && "Fetching comm info with use_persistent_session=false should succeed");
    std::cout << "  ✓ Tested with use_persistent_session=false\n";
    
    // Test with use_persistent_session = true (should also work)
    config.backend.use_persistent_session = true;
    result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    assert(result && "Fetching comm info with use_persistent_session=true should succeed");
    std::cout << "  ✓ Tested with use_persistent_session=true\n";
    
    std::cout << "✓ Test passed: Persistent session flag works correctly\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Communication Info Unit Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_communication_info_validity();
        test_successful_comm_info_fetch();
        test_missing_serial_number();
        test_missing_uuid();
        test_invalid_cert_path();
        test_invalid_backend_url();
        test_persistent_session_flag();
        
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
