#include "agent/restart_manager.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace agent;

Config create_test_config() {
    Config config;
    config.service.max_restart_attempts = 3;
    config.service.restart_base_delay_ms = 100;
    config.service.restart_max_delay_ms = 5000;
    config.service.restart_jitter_factor = 0.2;
    config.service.quarantine_duration_s = 10;
    return config;
}

void test_initial_state() {
    std::cout << "\n=== Test: Initial State ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::AllowRestart);
    assert(!restart_mgr->is_quarantined());
    
    auto state = restart_mgr->get_state();
    assert(state.restart_count == 0);
    assert(!state.in_quarantine);
    
    std::cout << "✓ Initial state is correct\n";
}

void test_restart_counting() {
    std::cout << "\n=== Test: Restart Counting ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    
    for (int i = 0; i < 3; i++) {
        restart_mgr->record_restart();
        auto state = restart_mgr->get_state();
        assert(state.restart_count == i + 1);
        std::cout << "  Restart count: " << state.restart_count << "\n";
    }
    
    std::cout << "✓ Restart counting works correctly\n";
}

void test_quarantine_trigger() {
    std::cout << "\n=== Test: Quarantine Trigger ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    
    // Record restarts up to limit - 1
    for (int i = 0; i < config.service.max_restart_attempts; i++) {
        restart_mgr->record_restart();
    }
    
    // At this point, restart_count == max_restart_attempts
    // Next should_restart() check should trigger quarantine
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::Quarantine);
    assert(restart_mgr->is_quarantined());
    
    std::cout << "✓ Quarantine triggers after max attempts\n";
}

void test_backoff_calculation() {
    std::cout << "\n=== Test: Backoff Calculation ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    
    int prev_delay = 0;
    for (int i = 0; i < 5; i++) {
        restart_mgr->record_restart();
        int delay = restart_mgr->calculate_restart_delay_ms(config);
        
        std::cout << "  Attempt " << (i + 1) << " delay: " << delay << "ms\n";
        
        // Delay should increase (with jitter, so not strictly)
        // Just verify it's within reasonable bounds
        assert(delay >= config.service.restart_base_delay_ms);
        assert(delay <= config.service.restart_max_delay_ms * 1.5);
        
        prev_delay = delay;
    }
    
    std::cout << "✓ Backoff calculation works correctly\n";
}

void test_reset() {
    std::cout << "\n=== Test: Reset After Stable Runtime ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    
    // Record some restarts
    for (int i = 0; i < 2; i++) {
        restart_mgr->record_restart();
    }
    
    auto state = restart_mgr->get_state();
    assert(state.restart_count == 2);
    
    // Reset
    restart_mgr->reset();
    
    state = restart_mgr->get_state();
    assert(state.restart_count == 0);
    assert(!state.in_quarantine);
    
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::AllowRestart);
    
    std::cout << "✓ Reset works correctly\n";
}

void test_persistence_conversion() {
    std::cout << "\n=== Test: Persistence State Conversion ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    
    // Record some restarts
    restart_mgr->record_restart();
    restart_mgr->record_restart();
    
    // Convert to persisted state
    auto persisted = restart_mgr->to_persisted();
    assert(persisted.restart_count == 2);
    assert(!persisted.in_quarantine);
    assert(persisted.last_restart_timestamp > 0);
    
    // Create new manager and load from persisted
    auto restart_mgr2 = create_restart_manager();
    restart_mgr2->load_from_persisted(persisted);
    
    auto state = restart_mgr2->get_state();
    assert(state.restart_count == 2);
    
    std::cout << "✓ Persistence conversion works correctly\n";
}

void test_quarantine_duration() {
    std::cout << "\n=== Test: Quarantine Duration ===\n";
    
    auto restart_mgr = create_restart_manager();
    Config config = create_test_config();
    config.service.quarantine_duration_s = 2;  // Short for testing
    
    // Trigger quarantine
    for (int i = 0; i < config.service.max_restart_attempts; i++) {
        restart_mgr->record_restart();
    }
    
    auto decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::Quarantine);
    
    // Should still be in quarantine immediately
    decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::QuarantineActive);
    
    // Wait for quarantine to expire
    std::cout << "  Waiting for quarantine to expire (2s)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Should be reset now
    decision = restart_mgr->should_restart(config);
    assert(decision == RestartDecision::AllowRestart);
    assert(!restart_mgr->is_quarantined());
    
    std::cout << "✓ Quarantine duration works correctly\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Restart Manager Unit Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_initial_state();
        test_restart_counting();
        test_quarantine_trigger();
        test_backoff_calculation();
        test_reset();
        test_persistence_conversion();
        test_quarantine_duration();
        
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
