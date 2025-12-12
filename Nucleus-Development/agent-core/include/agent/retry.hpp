#pragma once

#include <functional>
#include <chrono>
#include <memory>
#include "config.hpp"

namespace agent {

enum class CircuitState {
    Closed,      // Normal operation
    Open,        // Too many failures, fast-fail
    HalfOpen     // Testing recovery
};

class RetryPolicy {
public:
    virtual ~RetryPolicy() = default;
    
    // Execute operation with retry logic
    // Returns true if operation succeeded, false if all retries exhausted
    virtual bool execute(std::function<bool()> operation) = 0;
    
    // Get current circuit breaker state
    virtual CircuitState circuit_state() const = 0;
    
    // Reset policy (like for example after a successful operation)
    virtual void reset() = 0;
};

// Create retry policy with exponential backoff and jitter
std::unique_ptr<RetryPolicy> create_retry_policy(const Config::Retry& config);

// Utility function: Calculate exponential backoff with jitter
// attempt: 0-based attempt number (0 = first attempt, no delay)
// base_ms: base delay in milliseconds
// max_ms: maximum delay cap in milliseconds  
// jitter_pct: jitter percentage (e.g., 20 for ±20%)
int calculate_backoff_with_jitter(int attempt, int base_ms, int max_ms, int jitter_pct = 20);

}
