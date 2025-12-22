#include "agent/restart_manager.hpp"
#include "agent/restart_state_store.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <errno.h>
#else
#include <unistd.h>
#endif

using namespace agent;

#ifdef _WIN32
const std::string TEST_STATE_DIR = "C:\\temp\\agent-restart-test";
#else
const std::string TEST_STATE_DIR = "/tmp/agent-restart-test";
#endif
const std::string TEST_STATE_FILE = TEST_STATE_DIR + "/restart-state.json";

void cleanup_test_state() {
    std::remove(TEST_STATE_FILE.c_str());
#ifdef _WIN32
    _rmdir(TEST_STATE_DIR.c_str());
#else
    rmdir(TEST_STATE_DIR.c_str());
#endif
}

void ensure_state_dir() {
#ifdef _WIN32
    if (_mkdir(TEST_STATE_DIR.c_str()) != 0 && errno != EEXIST) {
        // Try to create parent directory if it doesn't exist
        std::string parent = TEST_STATE_DIR.substr(0, TEST_STATE_DIR.find_last_of("\\/"));
        if (!parent.empty()) {
            _mkdir(parent.c_str());
            _mkdir(TEST_STATE_DIR.c_str());
        }
    }
#else
    if (mkdir(TEST_STATE_DIR.c_str(), 0755) != 0 && errno != EEXIST) {
        // Directory creation failed and it's not because it already exists
        std::cerr << "Failed to create test directory: " << TEST_STATE_DIR << "\n";
    }
#endif
}

Config create_test_config() {
    Config config;
    config.service.max_restart_attempts = 3;
    config.service.restart_base_delay_ms = 50;
    config.service.restart_max_delay_ms = 500;
    config.service.restart_jitter_factor = 0.1;
    config.service.quarantine_duration_s = 5;
    return config;
}

void test_fresh_start() {
    std::cout << "\n=== Test: Fresh Start (No State File) ===\n";
    
    cleanup_test_state();
    ensure_state_dir();
    
    auto config = create_test_config();
    auto restart_store = create_restart_state_store(TEST_STATE_FILE);
    auto restart_mgr = create_restart_manager();
    
    // No state file should exist
    assert(!restart_store->exists());
    
    // Should allow restart
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::AllowRestart);
    
    // Record restart and save
    restart_mgr->record_restart();
    auto persisted = restart_mgr->to_persisted();
    assert(restart_store->save(persisted));
    
    // State file should now exist
    assert(restart_store->exists());
    
    std::cout << "✓ Fresh start works correctly\n";
}

void test_restart_with_existing_state() {
    std::cout << "\n=== Test: Restart With Existing State ===\n";
    
    auto config = create_test_config();
    auto restart_store = create_restart_state_store(TEST_STATE_FILE);
    auto restart_mgr = create_restart_manager();
    
    // Load existing state
    PersistedRestartState persisted;
    assert(restart_store->load(persisted));
    assert(persisted.restart_count == 1);
    
    restart_mgr->load_from_persisted(persisted);
    
    // Should still allow restart
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::AllowRestart);
    
    // Calculate and check backoff delay
    int delay = restart_mgr->calculate_restart_delay_ms(config);
    std::cout << "  Backoff delay: " << delay << "ms\n";
    assert(delay > 0);
    
    // Record another restart
    restart_mgr->record_restart();
    persisted = restart_mgr->to_persisted();
    assert(persisted.restart_count == 2);
    restart_store->save(persisted);
    
    std::cout << "✓ Restart with existing state works correctly\n";
}

void test_multiple_restarts_to_quarantine() {
    std::cout << "\n=== Test: Multiple Restarts Leading to Quarantine ===\n";
    
    auto config = create_test_config();
    auto restart_store = create_restart_state_store(TEST_STATE_FILE);
    
    // Simulate restart 3
    {
        auto restart_mgr = create_restart_manager();
        PersistedRestartState persisted;
        restart_store->load(persisted);
        restart_mgr->load_from_persisted(persisted);
        
        std::cout << "  Restart count before: " << persisted.restart_count << "\n";
        
        auto decision = restart_mgr->should_restart(config);
        assert(decision == RestartDecision::AllowRestart);
        
        restart_mgr->record_restart();
        persisted = restart_mgr->to_persisted();
        restart_store->save(persisted);
        
        std::cout << "  Restart count after: " << persisted.restart_count << "\n";
    }
    
    // Simulate restart 4 - should trigger quarantine
    {
        auto restart_mgr = create_restart_manager();
        PersistedRestartState persisted;
        restart_store->load(persisted);
        restart_mgr->load_from_persisted(persisted);
        
        std::cout << "  Restart count: " << persisted.restart_count << "\n";
        assert(persisted.restart_count == config.service.max_restart_attempts);
        
        auto decision = restart_mgr->should_restart(config);
        std::cout << "  Decision: ";
        if (decision == RestartDecision::Quarantine) {
            std::cout << "Quarantine\n";
        } else if (decision == RestartDecision::QuarantineActive) {
            std::cout << "QuarantineActive\n";
        } else {
            std::cout << "AllowRestart\n";
        }
        
        assert(decision == RestartDecision::Quarantine);
        assert(restart_mgr->is_quarantined());
        
        // Save quarantine state
        persisted = restart_mgr->to_persisted();
        assert(persisted.in_quarantine);
        restart_store->save(persisted);
    }
    
    std::cout << "✓ Quarantine triggered after max restarts\n";
}

void test_quarantine_persistence() {
    std::cout << "\n=== Test: Quarantine State Persists ===\n";
    
    auto config = create_test_config();
    auto restart_store = create_restart_state_store(TEST_STATE_FILE);
    auto restart_mgr = create_restart_manager();
    
    // Load quarantined state
    PersistedRestartState persisted;
    assert(restart_store->load(persisted));
    assert(persisted.in_quarantine);
    
    restart_mgr->load_from_persisted(persisted);
    
    // Should still be in quarantine
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::QuarantineActive);
    
    std::cout << "✓ Quarantine state persists across restarts\n";
}

void test_quarantine_expiration() {
    std::cout << "\n=== Test: Quarantine Expiration ===\n";
    
    auto config = create_test_config();
    config.service.quarantine_duration_s = 2;  // Short for testing
    
    auto restart_store = create_restart_state_store(TEST_STATE_FILE);
    auto restart_mgr = create_restart_manager();
    
    // Load quarantined state
    PersistedRestartState persisted;
    restart_store->load(persisted);
    restart_mgr->load_from_persisted(persisted);
    
    std::cout << "  Waiting for quarantine to expire (2s)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Should be out of quarantine now
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::AllowRestart);
    assert(!restart_mgr->is_quarantined());
    
    // Save reset state
    persisted = restart_mgr->to_persisted();
    assert(persisted.restart_count == 0);
    assert(!persisted.in_quarantine);
    restart_store->save(persisted);
    
    std::cout << "✓ Quarantine expires and state resets\n";
}

void test_stable_runtime_reset() {
    std::cout << "\n=== Test: Stable Runtime Reset ===\n";
    
    cleanup_test_state();
    ensure_state_dir();
    
    auto config = create_test_config();
    auto restart_store = create_restart_state_store(TEST_STATE_FILE);
    auto restart_mgr = create_restart_manager();
    
    // Record a few restarts
    for (int i = 0; i < 2; i++) {
        restart_mgr->record_restart();
    }
    
    auto persisted = restart_mgr->to_persisted();
    assert(persisted.restart_count == 2);
    restart_store->save(persisted);
    
    // Simulate stable runtime - reset counter
    restart_mgr->reset();
    persisted = restart_mgr->to_persisted();
    assert(persisted.restart_count == 0);
    restart_store->save(persisted);
    
    // Verify it persisted
    auto restart_mgr2 = create_restart_manager();
    PersistedRestartState loaded;
    restart_store->load(loaded);
    assert(loaded.restart_count == 0);
    
    std::cout << "✓ Stable runtime reset persists\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Restart/Quarantine Integration Tests\n";
    std::cout << "========================================\n";
    std::cout << "Testing crash recovery with persistent state\n";
    
    try {
        test_fresh_start();
        test_restart_with_existing_state();
        test_multiple_restarts_to_quarantine();
        test_quarantine_persistence();
        test_quarantine_expiration();
        test_stable_runtime_reset();
        
        cleanup_test_state();
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        cleanup_test_state();
        return 1;
    }
}
