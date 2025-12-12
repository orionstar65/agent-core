#include "agent/restart_manager.hpp"
#include "agent/restart_state_store.hpp"
#include "agent/retry.hpp"
#include <iostream>

namespace agent {

class RestartManagerImpl : public RestartManager {
public:
    RestartManagerImpl() = default;
    
    RestartDecision should_restart(const Config& config) override {
        auto now = std::chrono::steady_clock::now();
        
        // Check if in quarantine period
        if (state_.in_quarantine) {
            auto quarantine_duration = std::chrono::duration_cast<std::chrono::seconds>(
                now - state_.quarantine_start_time).count();
            
            if (quarantine_duration < config.service.quarantine_duration_s) {
                state_.last_decision = RestartDecision::QuarantineActive;
                return RestartDecision::QuarantineActive;
            } else {
                std::cout << "Agent Core: Quarantine period ended, resuming normal operation\n";
                reset();
            }
        }
        
        // Check if restart count exceeds limit
        if (state_.restart_count >= config.service.max_restart_attempts) {
            std::cout << "RestartManager: Max restart attempts ("
                      << config.service.max_restart_attempts
                      << ") exceeded, entering quarantine\n";
            state_.in_quarantine = true;
            state_.quarantine_start_time = now;
            state_.last_decision = RestartDecision::Quarantine;
            return RestartDecision::Quarantine;
        }
        
        state_.last_decision = RestartDecision::AllowRestart;
        return RestartDecision::AllowRestart;
    }
    
    void record_restart() override {
        state_.restart_count++;
        state_.last_restart_time = std::chrono::steady_clock::now();
    }
    
    void reset() override {
        state_.restart_count = 0;
        state_.in_quarantine = false;
        state_.last_decision = RestartDecision::AllowRestart;
    }
    
    RestartState get_state() const override {
        return state_;
    }
    
    int calculate_restart_delay_ms(const Config& config) const override {
        // Delegate to shared backoff calculation utility from retry.hpp
        int jitter_pct = static_cast<int>(config.service.restart_jitter_factor * 100);
        return calculate_backoff_with_jitter(
            state_.restart_count,
            config.service.restart_base_delay_ms,
            config.service.restart_max_delay_ms,
            jitter_pct
        );
    }
    
    bool is_quarantined() const override {
        return state_.in_quarantine;
    }
    
    void load_from_persisted(const PersistedRestartState& persisted) override {
        state_.restart_count = persisted.restart_count;
        state_.in_quarantine = persisted.in_quarantine;
        
        if (persisted.last_restart_timestamp > 0) {
            auto epoch_time = std::chrono::milliseconds(persisted.last_restart_timestamp);
            state_.last_restart_time = std::chrono::steady_clock::now() - 
                (std::chrono::system_clock::now().time_since_epoch() - epoch_time);
        }
        
        if (persisted.quarantine_start_timestamp > 0) {
            auto epoch_time = std::chrono::milliseconds(persisted.quarantine_start_timestamp);
            state_.quarantine_start_time = std::chrono::steady_clock::now() - 
                (std::chrono::system_clock::now().time_since_epoch() - epoch_time);
        }
    }
    
    PersistedRestartState to_persisted() const override {
        PersistedRestartState persisted;
        persisted.restart_count = state_.restart_count;
        persisted.in_quarantine = state_.in_quarantine;
        
        auto now_system = std::chrono::system_clock::now();
        auto now_steady = std::chrono::steady_clock::now();
        
        if (state_.restart_count > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now_steady - state_.last_restart_time);
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now_system.time_since_epoch() - elapsed);
            persisted.last_restart_timestamp = timestamp.count();
        }
        
        if (state_.in_quarantine) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now_steady - state_.quarantine_start_time);
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now_system.time_since_epoch() - elapsed);
            persisted.quarantine_start_timestamp = timestamp.count();
        }
        
        return persisted;
    }

private:
    RestartState state_;
};

std::unique_ptr<RestartManager> create_restart_manager() {
    return std::make_unique<RestartManagerImpl>();
}

}
