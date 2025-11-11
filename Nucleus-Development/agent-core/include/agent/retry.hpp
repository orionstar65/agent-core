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

}
