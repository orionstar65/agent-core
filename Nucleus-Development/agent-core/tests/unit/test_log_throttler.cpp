#include "agent/log_throttler.hpp"
#include "agent/telemetry.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace agent;

void test_throttling_activation() {
    std::cout << "\n=== Test: Throttling Activation ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 5;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // First 4 errors should not be throttled
    for (int i = 0; i < 4; i++) {
        bool should_throttle = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
        assert(!should_throttle && "First errors should not be throttled");
    }
    
    // 5th error triggers throttling but is still logged (not throttled)
    bool should_throttle = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    assert(!should_throttle && "5th error activates throttling but is still logged");
    
    // Subsequent errors (6th onwards) should be throttled
    for (int i = 0; i < 10; i++) {
        bool should_throttle = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
        assert(should_throttle && "Errors after threshold should be throttled");
    }
    
    int64_t throttled_count = throttler.get_throttled_count("TestSubsystem");
    assert(throttled_count == 10 && "Should have throttled 10 errors (after threshold)");
    
    std::cout << "✓ Throttling activates after threshold\n";
}

void test_per_subsystem_throttling() {
    std::cout << "\n=== Test: Per-Subsystem Throttling ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 3;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // Trigger throttling for Subsystem1 (first 3 errors)
    for (int i = 0; i < 3; i++) {
        bool result = throttler.should_throttle(LogLevel::Error, "Subsystem1");
        assert(!result && "First 3 errors for Subsystem1 not throttled");
    }
    // 4th error should be throttled
    assert(throttler.should_throttle(LogLevel::Error, "Subsystem1") && 
           "4th error for Subsystem1 should be throttled");
    
    // Subsystem2 should not be throttled yet (first 2 errors)
    for (int i = 0; i < 2; i++) {
        bool should_throttle = throttler.should_throttle(LogLevel::Error, "Subsystem2");
        assert(!should_throttle && "First 2 errors for Subsystem2 not throttled");
    }
    
    // 3rd error for Subsystem2 activates throttling but is still logged
    assert(!throttler.should_throttle(LogLevel::Error, "Subsystem2") && 
           "3rd error for Subsystem2 activates throttling but is still logged");
    // 4th error should be throttled
    assert(throttler.should_throttle(LogLevel::Error, "Subsystem2") && 
           "4th error for Subsystem2 should be throttled");
    
    std::cout << "✓ Per-subsystem throttling works independently\n";
}

void test_throttling_reset_on_success() {
    std::cout << "\n=== Test: Throttling Reset on Success ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 3;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // Trigger throttling
    for (int i = 0; i < 3; i++) {
        throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    }
    assert(throttler.should_throttle(LogLevel::Error, "TestSubsystem") && 
           "Should be throttled");
    
    // Record success - should reset throttling
    throttler.record_success("TestSubsystem");
    
    // Next error should not be throttled (but will increment count)
    bool should_throttle = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    assert(!should_throttle && "Should not be throttled after success");
    
    std::cout << "✓ Throttling resets on success\n";
}

void test_only_error_levels_throttled() {
    std::cout << "\n=== Test: Only Error Levels Throttled ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 1;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // Trigger throttling with errors
    throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    assert(throttler.should_throttle(LogLevel::Error, "TestSubsystem") && 
           "Errors should be throttled");
    
    // Other levels should not be throttled
    assert(!throttler.should_throttle(LogLevel::Info, "TestSubsystem") && 
           "Info should not be throttled");
    assert(!throttler.should_throttle(LogLevel::Warn, "TestSubsystem") && 
           "Warn should not be throttled");
    assert(!throttler.should_throttle(LogLevel::Debug, "TestSubsystem") && 
           "Debug should not be throttled");
    assert(throttler.should_throttle(LogLevel::Critical, "TestSubsystem") && 
           "Critical should be throttled");
    
    std::cout << "✓ Only ERROR and CRITICAL levels are throttled\n";
}

void test_throttling_disabled() {
    std::cout << "\n=== Test: Throttling Disabled ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = false;
    throttle_config.error_threshold = 1;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // Even with threshold of 1, should not throttle when disabled
    for (int i = 0; i < 10; i++) {
        bool should_throttle = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
        assert(!should_throttle && "Should not throttle when disabled");
    }
    
    std::cout << "✓ Throttling respects enabled flag\n";
}

void test_throttled_count_tracking() {
    std::cout << "\n=== Test: Throttled Count Tracking ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 3;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // Trigger throttling (first 3 errors)
    bool throttled1 = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    bool throttled2 = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    bool throttled3 = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    
    assert(!throttled1 && "First error should not be throttled");
    assert(!throttled2 && "Second error should not be throttled");
    assert(!throttled3 && "Third error activates throttling but is still logged");
    
    // Throttle 5 more errors (these are throttled)
    for (int i = 0; i < 5; i++) {
        bool result = throttler.should_throttle(LogLevel::Error, "TestSubsystem");
        assert(result && "Should be throttled after threshold");
    }
    
    // Throttled count should be 5 (errors after the threshold)
    int64_t throttled = throttler.get_throttled_count("TestSubsystem");
    assert(throttled == 5 && "Should have throttled 5 errors after threshold");
    
    std::cout << "✓ Throttled count tracked correctly\n";
}

void test_activation_flag() {
    std::cout << "\n=== Test: Activation Flag ===\n";
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 3;
    throttle_config.window_seconds = 60;
    
    LogThrottler throttler(throttle_config);
    
    // Trigger throttling
    for (int i = 0; i < 3; i++) {
        assert(!throttler.was_just_activated("TestSubsystem") && 
               "Should not be activated yet");
        throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    }
    
    // After threshold, should be activated
    throttler.should_throttle(LogLevel::Error, "TestSubsystem");
    assert(throttler.was_just_activated("TestSubsystem") && 
           "Should be just activated");
    
    // Flag should be cleared after reading
    assert(!throttler.was_just_activated("TestSubsystem") && 
           "Flag should be cleared");
    
    std::cout << "✓ Activation flag works correctly\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Log Throttler Unit Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_throttling_activation();
        test_per_subsystem_throttling();
        test_throttling_reset_on_success();
        test_only_error_levels_throttled();
        test_throttling_disabled();
        test_throttled_count_tracking();
        test_activation_flag();
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    } catch (...) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with unknown exception\n";
        std::cerr << "========================================\n";
        return 1;
    }
}

