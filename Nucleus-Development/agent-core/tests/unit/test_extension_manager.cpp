#include "agent/extension_manager.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using namespace agent;
using json = nlohmann::json;

const std::string TEST_DIR = "/tmp/agent-ext-test";
const std::string TEST_CONFIG_PATH = "../config/test_extensions.json";

struct TestExtConfig {
    std::string sample_extension_path;
    std::string sleep_script;
    std::string crash_script;
    bool use_real_extension{true};
    int startup_delay_ms{200};
    int crash_detection_delay_ms{500};
};

TestExtConfig g_test_ext_config;

bool load_test_extension_config() {
    try {
        std::ifstream file(TEST_CONFIG_PATH);
        if (!file) {
            g_test_ext_config.sample_extension_path = "../../extensions/sample/build/sample-ext";
            g_test_ext_config.sleep_script = "sleep 10";
            g_test_ext_config.crash_script = "exit 1";
            return false;
        }
        
        json j;
        file >> j;
        
        g_test_ext_config.sample_extension_path = j["extensions"]["sampleExtensionPath"].get<std::string>();
        g_test_ext_config.sleep_script = j["extensions"]["fallbackScripts"]["sleep"].get<std::string>();
        g_test_ext_config.crash_script = j["extensions"]["fallbackScripts"]["crash"].get<std::string>();
        g_test_ext_config.use_real_extension = j["tests"]["useRealExtension"].get<bool>();
        g_test_ext_config.startup_delay_ms = j["tests"]["extensionStartupDelayMs"].get<int>();
        g_test_ext_config.crash_detection_delay_ms = j["tests"]["crashDetectionDelayMs"].get<int>();
        
        std::cout << "Test config loaded: sample_path=" << g_test_ext_config.sample_extension_path << "\n";
        return true;
    } catch (const std::exception& e) {
        g_test_ext_config.sample_extension_path = "../../extensions/sample/build/sample-ext";
        g_test_ext_config.sleep_script = "sleep 10";
        g_test_ext_config.crash_script = "exit 1";
        return false;
    }
}

Config::Extensions create_test_config() {
    Config::Extensions config;
    config.manifest_path = TEST_DIR + "/extensions.json";
    config.max_restart_attempts = 2;
    config.restart_base_delay_ms = 100;
    config.restart_max_delay_ms = 500;
    config.quarantine_duration_s = 2;
    config.health_check_interval_s = 1;
    config.crash_detection_interval_s = 1;
    return config;
}

void setup_test_dir() {
    system(("rm -rf " + TEST_DIR).c_str());
    mkdir(TEST_DIR.c_str(), 0755);
}

void cleanup_test_dir() {
    system(("rm -rf " + TEST_DIR).c_str());
}

void create_test_extension(const std::string& name, const std::string& script) {
    std::string path = TEST_DIR + "/" + name;
    std::ofstream file(path);
    file << "#!/bin/bash\n" << script;
    file.close();
    chmod(path.c_str(), 0755);
}

void test_create_extension_manager() {
    std::cout << "\n=== Test: Create Extension Manager ===\n";
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    assert(ext_mgr != nullptr);
    
    auto status = ext_mgr->status();
    assert(status.empty());
    
    std::cout << "✓ Extension manager created successfully\n";
}

void test_launch_extension() {
    std::cout << "\n=== Test: Launch Extension ===\n";
    
    setup_test_dir();
    
    // Create a simple extension that sleeps
    create_test_extension("sleep-ext.sh", "sleep 10\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    ExtensionSpec spec;
    spec.name = "sleep-ext";
    spec.exec_path = TEST_DIR + "/sleep-ext.sh";
    spec.critical = true;
    spec.enabled = true;
    
    ext_mgr->launch({spec});
    
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto status = ext_mgr->status();
    assert(status.size() == 1);
    assert(status.count("sleep-ext") == 1);
    assert(status["sleep-ext"] == ExtState::Running);
    
    std::cout << "  Extension launched and running\n";
    
    // Clean up
    ext_mgr->stop_all();
    
    std::cout << "✓ Launch extension test passed\n";
    cleanup_test_dir();
}

void test_stop_extension() {
    std::cout << "\n=== Test: Stop Extension ===\n";
    
    setup_test_dir();
    create_test_extension("sleep-ext.sh", "sleep 10\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    ExtensionSpec spec;
    spec.name = "sleep-ext";
    spec.exec_path = TEST_DIR + "/sleep-ext.sh";
    spec.critical = true;
    spec.enabled = true;
    
    ext_mgr->launch({spec});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto status = ext_mgr->status();
    assert(status["sleep-ext"] == ExtState::Running);
    
    // Stop specific extension
    ext_mgr->stop("sleep-ext");
    
    status = ext_mgr->status();
    assert(status["sleep-ext"] == ExtState::Stopped);
    
    std::cout << "✓ Stop extension test passed\n";
    cleanup_test_dir();
}

void test_crash_detection() {
    std::cout << "\n=== Test: Crash Detection ===\n";
    
    setup_test_dir();
    
    // Create extension that exits immediately
    create_test_extension("crash-ext.sh", "exit 1\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    ExtensionSpec spec;
    spec.name = "crash-ext";
    spec.exec_path = TEST_DIR + "/crash-ext.sh";
    spec.critical = true;
    spec.enabled = true;
    
    ext_mgr->launch({spec});
    
    // Wait for process to exit and monitor to detect
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ext_mgr->monitor();
    
    auto status = ext_mgr->status();
    // Should be either Crashed, Running (restarted), or Quarantined
    assert(status.count("crash-ext") == 1);
    
    std::cout << "  State after crash: ";
    switch (status["crash-ext"]) {
        case ExtState::Running: std::cout << "Running (restarted)\n"; break;
        case ExtState::Crashed: std::cout << "Crashed\n"; break;
        case ExtState::Quarantined: std::cout << "Quarantined\n"; break;
        default: std::cout << "Unknown\n"; break;
    }
    
    ext_mgr->stop_all();
    std::cout << "✓ Crash detection test passed\n";
    cleanup_test_dir();
}

void test_quarantine_after_max_restarts() {
    std::cout << "\n=== Test: Quarantine After Max Restarts ===\n";
    
    setup_test_dir();
    
    // Extension that exits immediately (simulates repeated crashes)
    create_test_extension("always-crash.sh", "exit 1\n");
    
    auto config = create_test_config();
    config.max_restart_attempts = 2;
    config.restart_base_delay_ms = 50;
    
    auto ext_mgr = create_extension_manager(config);
    
    ExtensionSpec spec;
    spec.name = "always-crash";
    spec.exec_path = TEST_DIR + "/always-crash.sh";
    spec.critical = true;
    spec.enabled = true;
    
    ext_mgr->launch({spec});
    
    // Allow time for restarts and monitoring
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ext_mgr->monitor();
        
        auto status = ext_mgr->status();
        auto health = ext_mgr->health_status();
        
        std::cout << "  Iteration " << (i + 1) << " state: ";
        switch (status["always-crash"]) {
            case ExtState::Running: std::cout << "Running"; break;
            case ExtState::Crashed: std::cout << "Crashed"; break;
            case ExtState::Quarantined: std::cout << "Quarantined"; break;
            default: std::cout << "Unknown"; break;
        }
        std::cout << ", restart_count: " << health["always-crash"].restart_count << "\n";
        
        if (status["always-crash"] == ExtState::Quarantined) {
            break;
        }
    }
    
    auto status = ext_mgr->status();
    assert(status["always-crash"] == ExtState::Quarantined);
    
    ext_mgr->stop_all();
    std::cout << "✓ Quarantine after max restarts test passed\n";
    cleanup_test_dir();
}

void test_health_status() {
    std::cout << "\n=== Test: Health Status ===\n";
    
    setup_test_dir();
    create_test_extension("healthy-ext.sh", "sleep 10\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    ExtensionSpec spec;
    spec.name = "healthy-ext";
    spec.exec_path = TEST_DIR + "/healthy-ext.sh";
    spec.critical = true;
    spec.enabled = true;
    
    ext_mgr->launch({spec});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto health = ext_mgr->health_status();
    assert(health.count("healthy-ext") == 1);
    
    auto& ext_health = health["healthy-ext"];
    assert(ext_health.name == "healthy-ext");
    assert(ext_health.state == ExtState::Running);
    assert(ext_health.restart_count == 0);
    
    std::cout << "  Extension: " << ext_health.name << "\n";
    std::cout << "  State: Running\n";
    std::cout << "  Restart count: " << ext_health.restart_count << "\n";
    
    ext_mgr->stop_all();
    std::cout << "✓ Health status test passed\n";
    cleanup_test_dir();
}

void test_disabled_extension_not_launched() {
    std::cout << "\n=== Test: Disabled Extension Not Launched ===\n";
    
    setup_test_dir();
    create_test_extension("disabled-ext.sh", "sleep 10\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    ExtensionSpec spec;
    spec.name = "disabled-ext";
    spec.exec_path = TEST_DIR + "/disabled-ext.sh";
    spec.critical = true;
    spec.enabled = false;  // Disabled
    
    ext_mgr->launch({spec});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto status = ext_mgr->status();
    // Disabled extension should not appear in status
    assert(status.empty() || status.count("disabled-ext") == 0);
    
    std::cout << "✓ Disabled extension not launched\n";
    cleanup_test_dir();
}

void test_multiple_extensions() {
    std::cout << "\n=== Test: Multiple Extensions ===\n";
    
    setup_test_dir();
    create_test_extension("ext1.sh", "sleep 10\n");
    create_test_extension("ext2.sh", "sleep 10\n");
    create_test_extension("ext3.sh", "sleep 10\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    std::vector<ExtensionSpec> specs;
    
    for (int i = 1; i <= 3; i++) {
        ExtensionSpec spec;
        spec.name = "ext" + std::to_string(i);
        spec.exec_path = TEST_DIR + "/ext" + std::to_string(i) + ".sh";
        spec.critical = true;
        spec.enabled = true;
        specs.push_back(spec);
    }
    
    ext_mgr->launch(specs);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    auto status = ext_mgr->status();
    assert(status.size() == 3);
    
    for (int i = 1; i <= 3; i++) {
        std::string name = "ext" + std::to_string(i);
        assert(status.count(name) == 1);
        assert(status[name] == ExtState::Running);
        std::cout << "  " << name << ": Running\n";
    }
    
    ext_mgr->stop_all();
    std::cout << "✓ Multiple extensions test passed\n";
    cleanup_test_dir();
}

void test_stop_all_extensions() {
    std::cout << "\n=== Test: Stop All Extensions ===\n";
    
    setup_test_dir();
    create_test_extension("ext1.sh", "sleep 10\n");
    create_test_extension("ext2.sh", "sleep 10\n");
    
    auto config = create_test_config();
    auto ext_mgr = create_extension_manager(config);
    
    std::vector<ExtensionSpec> specs;
    for (int i = 1; i <= 2; i++) {
        ExtensionSpec spec;
        spec.name = "ext" + std::to_string(i);
        spec.exec_path = TEST_DIR + "/ext" + std::to_string(i) + ".sh";
        spec.critical = true;
        spec.enabled = true;
        specs.push_back(spec);
    }
    
    ext_mgr->launch(specs);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto status = ext_mgr->status();
    assert(status.size() == 2);
    assert(status["ext1"] == ExtState::Running);
    assert(status["ext2"] == ExtState::Running);
    
    // Stop all
    ext_mgr->stop_all();
    
    status = ext_mgr->status();
    assert(status["ext1"] == ExtState::Stopped);
    assert(status["ext2"] == ExtState::Stopped);
    
    std::cout << "✓ Stop all extensions test passed\n";
    cleanup_test_dir();
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Extension Manager Unit Tests\n";
    std::cout << "========================================\n";
    
    load_test_extension_config();
    
    try {
        test_create_extension_manager();
        test_launch_extension();
        test_stop_extension();
        test_crash_detection();
        test_quarantine_after_max_restarts();
        test_health_status();
        test_disabled_extension_not_launched();
        test_multiple_extensions();
        test_stop_all_extensions();
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        cleanup_test_dir();
        return 1;
    }
}
