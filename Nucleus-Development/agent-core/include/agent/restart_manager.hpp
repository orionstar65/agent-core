#pragma once

#include "agent/config.hpp"
#include "agent/restart_state_store.hpp"
#include <chrono>
#include <memory>

namespace agent {

enum class RestartDecision {
    AllowRestart,      // Restart is allowed
    Quarantine,        // Too many failures, enter quarantine
    QuarantineActive   // Currently in quarantine period
};

struct RestartState {
    int restart_count{0};
    std::chrono::steady_clock::time_point last_restart_time;
    std::chrono::steady_clock::time_point quarantine_start_time;
    bool in_quarantine{false};
    RestartDecision last_decision{RestartDecision::AllowRestart};
};

class RestartManager {
public:
    virtual ~RestartManager() = default;
    
    /// Check if restart should be allowed
    virtual RestartDecision should_restart(const Config& config) = 0;
    
    /// Record a restart attempt
    virtual void record_restart() = 0;
    
    /// Reset restart counter (on successful stable run)
    virtual void reset() = 0;
    
    /// Get current restart state
    virtual RestartState get_state() const = 0;
    
    /// Calculate delay before next restart (with backoff and jitter)
    virtual int calculate_restart_delay_ms(const Config& config) const = 0;
    
    /// Check if currently in quarantine
    virtual bool is_quarantined() const = 0;
    
    /// Load state from persistent storage
    virtual void load_from_persisted(const PersistedRestartState& persisted) = 0;
    
    /// Get persisted state for saving
    virtual PersistedRestartState to_persisted() const = 0;
};

/// Create restart manager implementation
std::unique_ptr<RestartManager> create_restart_manager();

}
