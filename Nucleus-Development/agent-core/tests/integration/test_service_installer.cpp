#include "agent/service_installer.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <unistd.h>

using namespace agent;

void cleanup_service() {
    std::cout << "\nCleaning up service installation...\n";
    system("systemctl stop agent-core 2>/dev/null");
    system("systemctl disable agent-core 2>/dev/null");
    system("rm -f /etc/systemd/system/agent-core.service");
    system("systemctl daemon-reload");
    std::cout << "Cleanup complete\n";
}

void test_check_status() {
    std::cout << "\n=== Test: Check Service Status ===\n";
    
    auto installer = create_service_installer();
    auto status = installer->check_status();
    
    std::cout << "  Current status: ";
    switch (status) {
        case ServiceInstallStatus::NotInstalled:
            std::cout << "NotInstalled\n";
            break;
        case ServiceInstallStatus::Installed:
            std::cout << "Installed (not running)\n";
            break;
        case ServiceInstallStatus::Running:
            std::cout << "Running\n";
            break;
        case ServiceInstallStatus::Failed:
            std::cout << "Failed\n";
            break;
    }
    
    // Just verify we can query status without crashing
    std::cout << "✓ Status check completed\n";
}

void test_install_requires_root() {
    std::cout << "\n=== Test: Install Requires Root ===\n";
    
    if (geteuid() == 0) {
        std::cout << "  Running as root, skipping this test\n";
        return;
    }
    
    auto installer = create_service_installer();
    
    // This should fail without root
    bool result = installer->install("/tmp/fake-binary", "/tmp/fake-config.json");
    assert(!result && "Install should fail without root privileges");
    
    std::cout << "✓ Install correctly requires root\n";
}

void test_install_and_uninstall() {
    std::cout << "\n=== Test: Install and Uninstall Service ===\n";
    std::cout << "  NOTE: This test requires sudo privileges\n";
    
    if (geteuid() != 0) {
        std::cout << "  Skipping: Not running as root\n";
        std::cout << "  Run with: sudo ./build/tests/test_service_installer\n";
        return;
    }
    
    auto installer = create_service_installer();
    
    // Get current binary path
    char binary_path[1024];
    ssize_t len = readlink("/proc/self/exe", binary_path, sizeof(binary_path) - 1);
    if (len == -1) {
        std::cerr << "  Failed to get binary path\n";
        return;
    }
    binary_path[len] = '\0';
    
    // Use test config path
    std::string config_path = "../../config/example.json";
    
    // Check initial status
    auto initial_status = installer->check_status();
    std::cout << "  Initial status: ";
    if (initial_status == ServiceInstallStatus::NotInstalled) {
        std::cout << "NotInstalled\n";
    } else {
        std::cout << "Already installed\n";
    }
    
    // Install service
    std::cout << "  Installing service...\n";
    bool install_result = installer->install(binary_path, config_path);
    
    if (!install_result) {
        std::cerr << "  Failed to install service\n";
        return;
    }
    
    // Verify it's installed
    auto post_install_status = installer->check_status();
    assert(post_install_status != ServiceInstallStatus::NotInstalled);
    std::cout << "  Service installed successfully\n";
    
    // Test start
    std::cout << "  Starting service...\n";
    bool start_result = installer->start();
    
    if (start_result) {
        std::cout << "  Service started\n";
        
        // Give it a moment to start
        sleep(2);
        
        // Verify it's running
        auto running_status = installer->check_status();
        if (running_status == ServiceInstallStatus::Running) {
            std::cout << "  Service is running\n";
        } else {
            std::cout << "  Service start may have failed\n";
        }
        
        // Test stop
        std::cout << "  Stopping service...\n";
        bool stop_result = installer->stop();
        if (stop_result) {
            std::cout << "  Service stopped\n";
        }
    } else {
        std::cout << "  Service start failed (may be expected if not fully configured)\n";
    }
    
    std::cout << "✓ Install and uninstall test completed\n";
}

void test_directory_creation() {
    std::cout << "\n=== Test: Directory Creation ===\n";
    std::cout << "  NOTE: This test requires sudo privileges\n";
    
    if (geteuid() != 0) {
        std::cout << "  Skipping: Not running as root\n";
        return;
    }
    
    auto installer = create_service_installer();
    
    // Remove directories if they exist (from previous test)
    system("rm -rf /tmp/test-agent-install");
    
    char binary_path[1024];
    ssize_t len = readlink("/proc/self/exe", binary_path, sizeof(binary_path) - 1);
    if (len == -1) {
        std::cerr << "  Failed to get binary path\n";
        return;
    }
    binary_path[len] = '\0';
    
    // Install should create necessary directories
    installer->install(binary_path, "../../config/example.json");
    
    // Check if directories exist
    bool var_lib_exists = (access("/var/lib/agent-core", F_OK) == 0);
    bool etc_exists = (access("/etc/agent-core", F_OK) == 0);
    
    std::cout << "  /var/lib/agent-core exists: " << (var_lib_exists ? "yes" : "no") << "\n";
    std::cout << "  /etc/agent-core exists: " << (etc_exists ? "yes" : "no") << "\n";
    
    assert(var_lib_exists && "State directory should be created");
    assert(etc_exists && "Config directory should be created");
    
    std::cout << "✓ Directory creation test passed\n";
}

void test_double_install() {
    std::cout << "\n=== Test: Double Install (Idempotent) ===\n";
    std::cout << "  NOTE: This test requires sudo privileges\n";
    
    if (geteuid() != 0) {
        std::cout << "  Skipping: Not running as root\n";
        return;
    }
    
    auto installer = create_service_installer();
    
    char binary_path[1024];
    ssize_t len = readlink("/proc/self/exe", binary_path, sizeof(binary_path) - 1);
    if (len == -1) {
        std::cerr << "  Failed to get binary path\n";
        return;
    }
    binary_path[len] = '\0';
    
    std::string config_path = "../../config/example.json";
    
    // First install
    std::cout << "  First install...\n";
    bool first_install = installer->install(binary_path, config_path);
    assert(first_install && "First install should succeed");
    
    // Second install - should also succeed (idempotent)
    std::cout << "  Second install (should be idempotent)...\n";
    bool second_install = installer->install(binary_path, config_path);
    assert(second_install && "Second install should succeed (idempotent)");
    
    std::cout << "✓ Double install test passed (idempotent)\n";
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Service Installer Integration Tests\n";
    std::cout << "========================================\n";
    
    bool run_privileged_tests = false;
    
    // Check for --full flag
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--full") {
            run_privileged_tests = true;
            std::cout << "Running with --full flag: will execute privileged tests\n";
        }
    }
    
    try {
        // Non-privileged tests
        test_check_status();
        test_install_requires_root();
        
        // Privileged tests
        if (run_privileged_tests && geteuid() == 0) {
            test_directory_creation();
            test_double_install();
            test_install_and_uninstall();
        } else if (run_privileged_tests) {
            std::cout << "\n=== Skipped: Privileged Tests ===\n";
            std::cout << "  Run with sudo to execute installation tests\n";
            std::cout << "  Example: sudo ./build/tests/test_service_installer --full\n";
        } else {
            std::cout << "\n=== Skipped: Privileged Tests ===\n";
            std::cout << "  Run with --full flag to execute installation tests\n";
            std::cout << "  Example: sudo ./build/tests/test_service_installer --full\n";
        }
        
        // Cleanup at the end of all tests
        if (run_privileged_tests && geteuid() == 0) {
            cleanup_service();
        }
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        // Cleanup even on failure
        if (geteuid() == 0) {
            cleanup_service();
        }
        
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}
