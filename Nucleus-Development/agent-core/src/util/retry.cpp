#include "agent/retry.hpp"
#include <iostream>
#include <thread>
#include <random>

namespace agent {

// Utility function for calculating exponential backoff with jitter
int calculate_backoff_with_jitter(int attempt, int base_ms, int max_ms, int jitter_pct) {
    // Exponential backoff
    int exponential = base_ms * (1 << attempt);
    int capped = std::min(exponential, max_ms);
    
    // Add jitter
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(-jitter_pct, jitter_pct);
    int jitter_val = dis(gen);
    int jitter = capped * jitter_val / 100;
    
    return capped + jitter;
}

class RetryPolicyImpl : public RetryPolicy {
public:
    explicit RetryPolicyImpl(const Config::Retry& config)
        : max_attempts_(config.max_attempts),
          base_ms_(config.base_ms),
          max_ms_(config.max_ms),
          circuit_state_(CircuitState::Closed),
          failure_count_(0) {
    }
    
    bool execute(std::function<bool()> operation) override {
        if (circuit_state_ == CircuitState::Open) {
            return false;
        }
        
        for (int attempt = 0; attempt < max_attempts_; ++attempt) {
            if (attempt > 0) {
                int delay_ms = calculate_backoff(attempt);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            
            if (operation()) {
                // Success
                reset();
                return true;
            }
            
            failure_count_++;
        }
        
        // Open circuit breaker after too many failures
        if (failure_count_ >= max_attempts_ * 2) {
            circuit_state_ = CircuitState::Open;
        }
        
        return false;
    }
    
    CircuitState circuit_state() const override {
        return circuit_state_;
    }
    
    void reset() override {
        failure_count_ = 0;
        circuit_state_ = CircuitState::Closed;
    }

private:
    int max_attempts_;
    int base_ms_;
    int max_ms_;
    CircuitState circuit_state_;
    int failure_count_;
    
    int calculate_backoff(int attempt) {
        // Delegate to shared utility function
        return calculate_backoff_with_jitter(attempt, base_ms_, max_ms_, 20);
    }
};

std::unique_ptr<RetryPolicy> create_retry_policy(const Config::Retry& config) {
    return std::make_unique<RetryPolicyImpl>(config);
}

}
