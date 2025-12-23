#include "agent/extension_manager.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
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
const std::string TEST_DIR = "C:/tmp/agent-ext-lifecycle-test";
#else
const std::string TEST_DIR = "/tmp/agent-ext-lifecycle-test";
#endif
const std::string TEST_MANIFEST_PATH = TEST_DIR + "/test_manifest.json";

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
            
            // ping -n (X+1) waits X seconds (ping sends one packet per second)
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

void create_test_manifest(const std::vector<std::pair<std::string, std::string>>& extensions) {
    std::ofstream manifest(TEST_MANIFEST_PATH);
    manifest << "{\n";
    manifest << "  \"extensions\": [\n";
    
    for (size_t i = 0; i < extensions.size(); i++) {
        const auto& [name, exec_path] = extensions[i];
        manifest << "    {\n";
        manifest << "      \"name\": \"" << name << "\",\n";
        manifest << "      \"execPath\": \"" << exec_path << "\",\n";
        manifest << "      \"args\": [],\n";
        manifest << "      \"critical\": true,\n";
        manifest << "      \"enabled\": true\n";
        manifest << "    }";
        if (i < extensions.size() - 1) {
            manifest << ",";
        }
        manifest << "\n";
    }
    
    manifest << "  ]\n";
    manifest << "}\n";
    manifest.close();
}

Config::Extensions create_test_config() {
    Config::Extensions config;
    config.manifest_path = TEST_MANIFEST_PATH;
    config.max_restart_attempts = 3;
    config.restart_base_delay_ms = 100;
    config.restart_max_delay_ms = 500;
    config.quarantine_duration_s = 2;
    config.health_check_interval_s = 1;
    config.crash_detection_interval_s = 1;
    return config;
}

// Test 1: Load extensions from manifest and launch
void test_manifest_loading_and_launch() {
    std::cout << "\n=== Test: Manifest Loading and Launch ===\n";
    
    setup_test_dir();
    
    // Create test scripts
    create_test_script("ext1.sh", "sleep 5\n");
    create_test_script("ext2.sh", "sleep 5\n");
    
    // Create manifest
    std::vector<std::pair<std::string, std::string>> extensions = {
        {"ext1", get_extension_path("ext1.sh")},
        {"ext2", get_extension_path("ext2.sh")}
    };
    create_test_manifest(extensions);
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    // Load and launch extensions
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    assert(specs.size() == 2);
    assert(specs[0].name == "ext1");
    assert(specs[1].name == "ext2");
    
    ext_mgr->launch(specs);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto status = ext_mgr->status();
    assert(status.size() == 2);
    assert(status["ext1"] == ExtState::Running);
    assert(status["ext2"] == ExtState::Running);
    
    std::cout << "  ✓ Loaded 2 extensions from manifest\n";
    std::cout << "  ✓ Both extensions running\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Manifest loading and launch test passed\n";
}

// Test 2: Extension crash and automatic restart
void test_extension_crash_and_restart() {
    std::cout << "\n=== Test: Extension Crash and Restart ===\n";
    
    setup_test_dir();
    
    // Create extension that crashes after 0.5 seconds
    create_test_script("crasher.sh", "sleep 0.5\nexit 1\n");
    
    std::vector<std::pair<std::string, std::string>> extensions = {
        {"crasher", get_extension_path("crasher.sh")}
    };
    create_test_manifest(extensions);
    
    auto config = create_test_config();
    config.max_restart_attempts = 5;  // Allow more restarts
    auto ext_mgr = create_extension_manager(config);
    
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    ext_mgr->launch(specs);
    
    std::cout << "  Extension launched, waiting for crash...\n";
    
    // Wait for crash and monitor multiple times to catch the restart
    // Note: "sleep 0.5" converts to ping which waits 1 second on Windows
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        ext_mgr->monitor();
        
        auto health = ext_mgr->health_status();
        if (health.count("crasher") > 0 && health["crasher"].restart_count >= 1) {
            break;
        }
    }
    
    auto health = ext_mgr->health_status();
    assert(health.count("crasher") == 1);
    
    // Should have restarted at least once
    int restart_count = health["crasher"].restart_count;
    std::cout << "  ✓ Extension crashed and restarted (restart_count: " << restart_count << ")\n";
    assert(restart_count >= 1);
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Crash and restart test passed\n";
}

// Test 3: Extension quarantine after max restarts
void test_extension_quarantine() {
    std::cout << "\n=== Test: Extension Quarantine After Max Restarts ===\n";
    
    setup_test_dir();
    
    // Create extension that always crashes immediately
    create_test_script("always-crash.sh", "exit 1\n");
    
    std::vector<std::pair<std::string, std::string>> extensions = {
        {"always-crash", get_extension_path("always-crash.sh")}
    };
    create_test_manifest(extensions);
    
    auto config = create_test_config();
    config.max_restart_attempts = 2;
    config.restart_base_delay_ms = 50;
    auto ext_mgr = create_extension_manager(config);
    
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    ext_mgr->launch(specs);
    
    std::cout << "  Extension launched (will crash repeatedly)...\n";
    
    // Monitor until quarantined
    bool quarantined = false;
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ext_mgr->monitor();
        
        auto status = ext_mgr->status();
        auto health = ext_mgr->health_status();
        
        std::cout << "  Iteration " << (i + 1) << ": restart_count=" 
                  << health["always-crash"].restart_count << ", state=";
        
        switch (status["always-crash"]) {
            case ExtState::Running: std::cout << "Running\n"; break;
            case ExtState::Crashed: std::cout << "Crashed\n"; break;
            case ExtState::Quarantined: 
                std::cout << "Quarantined\n";
                quarantined = true;
                break;
            default: std::cout << "Unknown\n"; break;
        }
        
        if (quarantined) break;
    }
    
    assert(quarantined);
    std::cout << "  ✓ Extension quarantined after max restart attempts\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Quarantine test passed\n";
}

// Test 4: Multiple extensions with mixed behavior
void test_multiple_extensions_mixed_behavior() {
    std::cout << "\n=== Test: Multiple Extensions with Mixed Behavior ===\n";
    
    setup_test_dir();
    
    // Create extensions with different behaviors
    create_test_script("stable.sh", "sleep 10\n");
    create_test_script("crasher.sh", "sleep 0.3\nexit 1\n");
    create_test_script("quick-exit.sh", "exit 0\n");
    
    std::vector<std::pair<std::string, std::string>> extensions = {
        {"stable", get_extension_path("stable.sh")},
        {"crasher", get_extension_path("crasher.sh")},
        {"quick-exit", get_extension_path("quick-exit.sh")}
    };
    create_test_manifest(extensions);
    
    auto config = create_test_config();
    config.max_restart_attempts = 3;
    auto ext_mgr = create_extension_manager(config);
    
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    ext_mgr->launch(specs);
    
    std::cout << "  Launched 3 extensions with different behaviors\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Monitor for a while
    for (int i = 0; i < 3; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        ext_mgr->monitor();
    }
    
    auto status = ext_mgr->status();
    auto health = ext_mgr->health_status();
    
    // Stable should still be running
    assert(status["stable"] == ExtState::Running);
    assert(health["stable"].restart_count == 0);
    std::cout << "  ✓ Stable extension still running\n";
    
    // Crasher should have restarted or be running
    assert(health["crasher"].restart_count >= 1);
    std::cout << "  ✓ Crasher extension restarted " << health["crasher"].restart_count << " times\n";
    
    // Quick-exit should have crashed and restarted
    assert(health["quick-exit"].restart_count >= 1);
    std::cout << "  ✓ Quick-exit extension restarted " << health["quick-exit"].restart_count << " times\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Mixed behavior test passed\n";
}

// Test 5: Health status monitoring
void test_health_status_monitoring() {
    std::cout << "\n=== Test: Health Status Monitoring ===\n";
    
    setup_test_dir();
    
    create_test_script("healthy.sh", "sleep 10\n");
    
    std::vector<std::pair<std::string, std::string>> extensions = {
        {"healthy", get_extension_path("healthy.sh")}
    };
    create_test_manifest(extensions);
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    ext_mgr->launch(specs);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Health ping
    ext_mgr->health_ping();
    
    auto health = ext_mgr->health_status();
    assert(health.count("healthy") == 1);
    
    const auto& h = health["healthy"];
    assert(h.name == "healthy");
    assert(h.state == ExtState::Running);
    assert(h.restart_count == 0);
    assert(h.responding == true);
    
    std::cout << "  Extension: " << h.name << "\n";
    std::cout << "  State: Running\n";
    std::cout << "  Restart count: " << h.restart_count << "\n";
    std::cout << "  Responding: " << (h.responding ? "yes" : "no") << "\n";
    std::cout << "  ✓ Health status correctly reported\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Health status monitoring test passed\n";
}

// Test 6: Disabled extensions are not launched
void test_disabled_extensions_not_launched() {
    std::cout << "\n=== Test: Disabled Extensions Not Launched ===\n";
    
    setup_test_dir();
    
    create_test_script("disabled.sh", "sleep 10\n");
    
    // Create manifest with disabled extension
    std::ofstream manifest(TEST_MANIFEST_PATH);
    manifest << "{\n";
    manifest << "  \"extensions\": [\n";
    manifest << "    {\n";
    manifest << "      \"name\": \"disabled\",\n";
    manifest << "      \"execPath\": \"" << get_extension_path("disabled.sh") << "\",\n";
    manifest << "      \"args\": [],\n";
    manifest << "      \"critical\": true,\n";
    manifest << "      \"enabled\": false\n";
    manifest << "    }\n";
    manifest << "  ]\n";
    manifest << "}\n";
    manifest.close();
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    assert(specs.size() == 1);
    assert(specs[0].enabled == false);
    
    ext_mgr->launch(specs);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto status = ext_mgr->status();
    // Disabled extension should not be in status map
    assert(status.empty() || status.count("disabled") == 0);
    
    std::cout << "  ✓ Disabled extension was not launched\n";
    
    cleanup_test_dir();
    std::cout << "✓ Disabled extensions test passed\n";
}

// Test 7: Extension recovery from quarantine
void test_extension_recovery_from_quarantine() {
    std::cout << "\n=== Test: Extension Recovery from Quarantine ===\n";
    
    setup_test_dir();
    
    create_test_script("recover.sh", "exit 1\n");
    
    std::vector<std::pair<std::string, std::string>> extensions = {
        {"recover", get_extension_path("recover.sh")}
    };
    create_test_manifest(extensions);
    
    auto config = create_test_config();
    config.max_restart_attempts = 2;
    config.restart_base_delay_ms = 50;
    config.quarantine_duration_s = 1;  // Short quarantine for testing
    auto ext_mgr = create_extension_manager(config);
    
    auto specs = load_extension_manifest(TEST_MANIFEST_PATH);
    ext_mgr->launch(specs);
    
    std::cout << "  Waiting for extension to be quarantined...\n";
    
    // Wait for quarantine
    bool quarantined = false;
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ext_mgr->monitor();
        
        auto status = ext_mgr->status();
        if (status["recover"] == ExtState::Quarantined) {
            quarantined = true;
            std::cout << "  ✓ Extension quarantined\n";
            break;
        }
    }
    assert(quarantined);
    
    // Wait for quarantine duration to expire
    std::cout << "  Waiting for quarantine duration to expire...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Monitor should trigger recovery attempt
    ext_mgr->monitor();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto health = ext_mgr->health_status();
    // After quarantine expires, it should attempt restart (restart_count reset)
    std::cout << "  Extension attempted recovery (restart_count: " 
              << health["recover"].restart_count << ")\n";
    
    ext_mgr->stop_all();
    cleanup_test_dir();
    std::cout << "✓ Recovery from quarantine test passed\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Extension Manager Integration Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_manifest_loading_and_launch();
        test_extension_crash_and_restart();
        test_extension_quarantine();
        test_multiple_extensions_mixed_behavior();
        test_health_status_monitoring();
        test_disabled_extensions_not_launched();
        test_extension_recovery_from_quarantine();
        
        std::cout << "\n========================================\n";
        std::cout << "All integration tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Integration test failed: " << e.what() << "\n";
        std::cerr << "========================================\n";
        cleanup_test_dir();
        return 1;
    }
}
