#include "agent/retry.hpp"
#include <iostream>
#include <thread>
#include <random>

namespace agent {

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
            std::cout << "RetryPolicy: Circuit breaker OPEN, fast-failing\n";
            return false;
        }
        
        for (int attempt = 0; attempt < max_attempts_; ++attempt) {
            if (attempt > 0) {
                int delay_ms = calculate_backoff(attempt);
                std::cout << "RetryPolicy: Attempt " << (attempt + 1) 
                          << "/" << max_attempts_ 
                          << " after " << delay_ms << "ms\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            
            if (operation()) {
                // Success
                reset();
                return true;
            }
            
            failure_count_++;
        }
        
        // all attempts exhausted
        std::cout << "RetryPolicy: All retry attempts exhausted\n";
        
        // Open circuit breaker after too many failures
        if (failure_count_ >= max_attempts_ * 2) {
            circuit_state_ = CircuitState::Open;
            std::cout << "RetryPolicy: Circuit breaker opened after " 
                      << failure_count_ << " failures\n";
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
        // Exponential backoff with jitter
        int exponential = base_ms_ * (1 << attempt);
        int capped = std::min(exponential, max_ms_);
        
        // add jitter (±20%)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(-20, 20);
        int jitter_pct = dis(gen);
        int jitter = capped * jitter_pct / 100;
        
        return capped + jitter;
    }
};

std::unique_ptr<RetryPolicy> create_retry_policy(const Config::Retry& config) {
    return std::make_unique<RetryPolicyImpl>(config);
}

}
