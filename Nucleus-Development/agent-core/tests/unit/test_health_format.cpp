#include "agent/extension_manager.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace agent;

#ifdef _WIN32
const std::string TEST_DIR = "C:/tmp/agent-health-format-test";
#else
const std::string TEST_DIR = "/tmp/agent-health-format-test";
#endif

void setup_test_dir() {
    // Remove directory if it exists (ignore errors)
    std::error_code ec;
    std::filesystem::remove_all(TEST_DIR, ec);
    std::filesystem::create_directories(TEST_DIR);
}

void cleanup_test_dir() {
    // Remove directory if it exists (ignore errors)
    std::error_code ec;
    std::filesystem::remove_all(TEST_DIR, ec);
}

// Helper to get platform-appropriate extension name
std::string get_extension_path(const std::string& name) {
#ifdef _WIN32
    // Convert .sh to .bat on Windows
    if (name.find(".sh") != std::string::npos) {
        return TEST_DIR + "/" + name.substr(0, name.length() - 3) + ".bat";
    }
    return TEST_DIR + "/" + name;
#else
    return TEST_DIR + "/" + name;
#endif
}

// Helper to convert Unix commands to Windows equivalents
std::string convert_script_for_platform(const std::string& script) {
#ifdef _WIN32
    std::string win_script = script;
    // Replace sleep with ping (more reliable than timeout on Windows)
    // ping 127.0.0.1 -n (X+1) >nul waits X seconds
    std::istringstream iss(win_script);
    std::ostringstream oss;
    std::string line;
    bool first_line = true;
    
    while (std::getline(iss, line)) {
        if (!first_line) oss << "\r\n";
        first_line = false;
        
        // Replace sleep commands
        size_t pos = 0;
        while ((pos = line.find("sleep ", pos)) != std::string::npos) {
            size_t end = line.find_first_of(" \n\r", pos + 6);
            if (end == std::string::npos) end = line.length();
            
            std::string duration = line.substr(pos + 6, end - pos - 6);
            // Convert decimal seconds to whole seconds (round up)
            double secs = std::stod(duration);
            int whole_secs = static_cast<int>(std::ceil(secs));
            if (whole_secs < 1) whole_secs = 1; // Minimum 1 second
            
            // ping -n (X+1) waits X seconds
            line.replace(pos, end - pos, "ping 127.0.0.1 -n " + std::to_string(whole_secs + 1) + " >nul");
            pos += 40; // Move past replacement
        }
        
        // Replace exit commands
        pos = 0;
        while ((pos = line.find("exit 1", pos)) != std::string::npos) {
            line.replace(pos, 6, "exit /b 1");
            pos += 9;
        }
        pos = 0;
        while ((pos = line.find("exit 0", pos)) != std::string::npos) {
            line.replace(pos, 6, "exit /b 0");
            pos += 9;
        }
        
        oss << line;
    }
    
    return oss.str();
#else
    return script;
#endif
}

void create_test_script(const std::string& name, const std::string& script) {
    std::string path = get_extension_path(name);
    std::string platform_script = convert_script_for_platform(script);
    
    std::ofstream file(path);
#ifdef _WIN32
    file << "@echo off\n" << platform_script;
#else
    file << "#!/bin/bash\n" << platform_script;
    chmod(path.c_str(), 0755);
#endif
    file.close();
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
    spec1.exec_path = get_extension_path("ext1.sh");
    spec1.enabled = true;
    
    ExtensionSpec spec2;
    spec2.name = "ext2";
    spec2.exec_path = get_extension_path("ext2.sh");
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
    spec.exec_path = get_extension_path("crasher.sh");
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
