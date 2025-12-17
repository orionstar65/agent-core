#include "agent/retry.hpp"
#include "agent/telemetry.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace agent;

// Simple metrics implementation for testing
class TestMetrics : public Metrics {
public:
    void increment(const std::string& name, int64_t value = 1) override {
        counters_[name] += value;
    }
    
    void histogram(const std::string& name, double value) override {
        histograms_[name].push_back(value);
    }
    
    void gauge(const std::string& name, double value) override {
        gauges_[name] = value;
    }
    
    int64_t get_counter(const std::string& name) const {
        auto it = counters_.find(name);
        return (it != counters_.end()) ? it->second : 0;
    }

private:
    std::map<std::string, int64_t> counters_;
    std::map<std::string, std::vector<double>> histograms_;
    std::map<std::string, double> gauges_;
};

void test_retry_attempts_metric() {
    std::cout << "\n=== Test: Retry Attempts Metric ===\n";
    
    Config::Retry retry_config;
    retry_config.max_attempts = 3;
    retry_config.base_ms = 10;
    retry_config.max_ms = 100;
    
    auto metrics = std::make_unique<TestMetrics>();
    TestMetrics* metrics_ptr = metrics.get();
    
    auto retry_policy = create_retry_policy(retry_config, metrics_ptr);
    
    // Operation that fails all attempts
    bool result = retry_policy->execute([&]() {
        return false; // Always fail
    });
    
    assert(!result && "Operation should fail");
    assert(metrics_ptr->get_counter("retry.attempts") == 3 && 
           "Should have 3 attempts");
    assert(metrics_ptr->get_counter("retry.failures") == 1 && 
           "Should have 1 failure");
    assert(metrics_ptr->get_counter("retry.success") == 0 && 
           "Should have 0 successes");
    
    std::cout << "✓ Retry attempts metric tracked correctly\n";
}

void test_retry_success_metric() {
    std::cout << "\n=== Test: Retry Success Metric ===\n";
    
    Config::Retry retry_config;
    retry_config.max_attempts = 5;
    retry_config.base_ms = 10;
    retry_config.max_ms = 100;
    
    auto metrics = std::make_unique<TestMetrics>();
    TestMetrics* metrics_ptr = metrics.get();
    
    auto retry_policy = create_retry_policy(retry_config, metrics_ptr);
    
    int attempt_count = 0;
    // Operation that succeeds on 3rd attempt
    bool result = retry_policy->execute([&]() {
        attempt_count++;
        return attempt_count >= 3; // Succeed on 3rd attempt
    });
    
    assert(result && "Operation should succeed");
    assert(metrics_ptr->get_counter("retry.attempts") == 3 && 
           "Should have 3 attempts");
    assert(metrics_ptr->get_counter("retry.success") == 1 && 
           "Should have 1 success");
    assert(metrics_ptr->get_counter("retry.failures") == 0 && 
           "Should have 0 failures");
    
    std::cout << "✓ Retry success metric tracked correctly\n";
}

void test_retry_circuit_breaker_metric() {
    std::cout << "\n=== Test: Circuit Breaker Metric ===\n";
    
    Config::Retry retry_config;
    retry_config.max_attempts = 2;
    retry_config.base_ms = 10;
    retry_config.max_ms = 100;
    
    auto metrics = std::make_unique<TestMetrics>();
    TestMetrics* metrics_ptr = metrics.get();
    
    auto retry_policy = create_retry_policy(retry_config, metrics_ptr);
    
    // Fail enough times to open circuit breaker (max_attempts * 2 = 4 failures)
    for (int i = 0; i < 3; i++) {
        retry_policy->execute([&]() {
            return false; // Always fail
        });
    }
    
    // Circuit should be open now
    assert(retry_policy->circuit_state() == CircuitState::Open && 
           "Circuit should be open");
    assert(metrics_ptr->get_counter("retry.circuit_open") >= 1 && 
           "Should have circuit open event");
    
    std::cout << "✓ Circuit breaker metric tracked correctly\n";
}

void test_retry_without_metrics() {
    std::cout << "\n=== Test: Retry Without Metrics ===\n";
    
    Config::Retry retry_config;
    retry_config.max_attempts = 3;
    retry_config.base_ms = 10;
    retry_config.max_ms = 100;
    
    // Create retry policy without metrics (nullptr)
    auto retry_policy = create_retry_policy(retry_config, nullptr);
    
    // Should still work without metrics
    bool result = retry_policy->execute([&]() {
        return true; // Succeed immediately
    });
    
    assert(result && "Should work without metrics");
    
    std::cout << "✓ Retry works without metrics (backward compatible)\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Retry Metrics Unit Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_retry_attempts_metric();
        test_retry_success_metric();
        test_retry_circuit_breaker_metric();
        test_retry_without_metrics();
        
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

