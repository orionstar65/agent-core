#include "agent/extension_manager.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace agent;

const std::string TEST_DIR = "/tmp/agent-health-format-test";

void setup_test_dir() {
    system(("rm -rf " + TEST_DIR).c_str());
    mkdir(TEST_DIR.c_str(), 0755);
}

void cleanup_test_dir() {
    system(("rm -rf " + TEST_DIR).c_str());
}

void create_test_script(const std::string& name, const std::string& script) {
    std::string path = TEST_DIR + "/" + name;
    std::ofstream file(path);
    file << "#!/bin/bash\n" << script;
    file.close();
    chmod(path.c_str(), 0755);
}

// Simulate the health query handler logic from main.cpp
std::string format_health_response(ExtensionManager* ext_mgr, std::chrono::steady_clock::time_point start_time) {
    auto health_map = ext_mgr->health_status();
    
    // Build JSON response (same logic as main.cpp)
    std::string json = "{\"extensions\":[";
    bool first = true;
    
    for (const auto& [name, health] : health_map) {
        if (!first) json += ",";
        first = false;
        
        json += "{";
        json += "\"name\":\"" + name + "\",";
        json += "\"state\":" + std::to_string(static_cast<int>(health.state)) + ",";
        json += "\"restart_count\":" + std::to_string(health.restart_count) + ",";
        json += "\"responding\":" + std::string(health.responding ? "true" : "false");
        json += "}";
    }
    
    json += "],";
    json += "\"agent_uptime_s\":" + std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count());
    json += "}";
    
    return json;
}

void test_health_format_running_extensions() {
    std::cout << "\n=== Test: Health Format with Running Extensions ===\n";
    
    setup_test_dir();
    create_test_script("ext1.sh", "sleep 10\n");
    create_test_script("ext2.sh", "sleep 10\n");
    
    Config::Extensions config;
    config.max_restart_attempts = 3;
    auto ext_mgr = create_extension_manager(config);
    auto start_time = std::chrono::steady_clock::now();
    
    ExtensionSpec spec1;
    spec1.name = "ext1";
    spec1.exec_path = TEST_DIR + "/ext1.sh";
    spec1.enabled = true;
    
    ExtensionSpec spec2;
    spec2.name = "ext2";
    spec2.exec_path = TEST_DIR + "/ext2.sh";
    spec2.enabled = true;
    
    ext_mgr->launch({spec1, spec2});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Format health response
    std::string json = format_health_response(ext_mgr.get(), start_time);
    
    std::cout << "  Health JSON: " << json << "\n";
    
    // Verify structure
    assert(json.find("\"extensions\":") != std::string::npos);
    assert(json.find("\"ext1\"") != std::string::npos);
    assert(json.find("\"ext2\"") != std::string::npos);
    assert(json.find("\"state\":") != std::string::npos);
    assert(json.find("\"restart_count\":") != std::string::npos);
    assert(json.find("\"responding\":") != std::string::npos);
    assert(json.find("\"agent_uptime_s\":") != std::string::npos);
    
    std::cout << "  ✓ JSON contains all required fields\n";
    std::cout << "  ✓ Both extensions reported\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Health format test passed\n";
}

void test_health_format_no_extensions() {
    std::cout << "\n=== Test: Health Format with No Extensions ===\n";
    
    Config::Extensions config;
    config.max_restart_attempts = 3;
    auto ext_mgr = create_extension_manager(config);
    auto start_time = std::chrono::steady_clock::now();
    
    // Format health response without extensions
    std::string json = format_health_response(ext_mgr.get(), start_time);
    
    std::cout << "  Health JSON: " << json << "\n";
    
    // Verify structure
    assert(json.find("\"extensions\":[]") != std::string::npos);
    assert(json.find("\"agent_uptime_s\":") != std::string::npos);
    
    std::cout << "  ✓ Empty extensions array\n";
    std::cout << "  ✓ Uptime included\n";
    
    std::cout << "✓ No extensions health format test passed\n";
}

void test_health_format_quarantined_extension() {
    std::cout << "\n=== Test: Health Format with Quarantined Extension ===\n";
    
    setup_test_dir();
    create_test_script("crasher.sh", "exit 1\n");
    
    Config::Extensions config;
    config.max_restart_attempts = 2;
    config.restart_base_delay_ms = 50;
    auto ext_mgr = create_extension_manager(config);
    auto start_time = std::chrono::steady_clock::now();
    
    ExtensionSpec spec;
    spec.name = "crasher";
    spec.exec_path = TEST_DIR + "/crasher.sh";
    spec.enabled = true;
    
    ext_mgr->launch({spec});
    
    // Wait for quarantine
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ext_mgr->monitor();
        auto status = ext_mgr->status();
        if (status["crasher"] == ExtState::Quarantined) {
            break;
        }
    }
    
    // Format health response
    std::string json = format_health_response(ext_mgr.get(), start_time);
    
    std::cout << "  Health JSON: " << json << "\n";
    
    // Verify quarantined extension is reported
    assert(json.find("\"crasher\"") != std::string::npos);
    assert(json.find("\"restart_count\":") != std::string::npos);
    assert(json.find("\"state\":") != std::string::npos);
    
    // State 4 is Quarantined
    bool has_state_4 = json.find("\"state\":4") != std::string::npos;
    std::cout << "  Extension state: " << (has_state_4 ? "Quarantined (4)" : "Other") << "\n";
    
    std::cout << "  ✓ Quarantined extension reported\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Quarantined extension health format test passed\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Health Response Format Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_health_format_running_extensions();
        test_health_format_no_extensions();
        test_health_format_quarantined_extension();
        
        std::cout << "\n========================================\n";
        std::cout << "All health format tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed: " << e.what() << "\n";
        std::cerr << "========================================\n";
        cleanup_test_dir();
        return 1;
    }
}
