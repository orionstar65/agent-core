#include "agent/log_throttler.hpp"
#include <iostream>
#include <chrono>

namespace agent {

LogThrottler::LogThrottler(const Config::Logging::Throttle& config, Metrics* metrics)
    : config_(config), metrics_(metrics) {
}

bool LogThrottler::should_throttle(LogLevel level, const std::string& subsystem) {
    // Only throttle ERROR and CRITICAL levels
    if (level != LogLevel::Error && level != LogLevel::Critical) {
        return false;
    }
    
    // If throttling is disabled, don't throttle
    if (!config_.enabled) {
        return false;
    }
    
    // Get or create subsystem state
    auto& state = subsystem_states_[subsystem];
    update_window(subsystem);
    
    // Increment error count
    state.error_count++;
    
    // Check if we should start throttling (AFTER this error)
    if (!state.is_throttled && state.error_count >= config_.error_threshold) {
        state.is_throttled = true;
        state.just_activated = true;
        // Don't throttle this error - let it through as the last one before throttling
        return false;
    }
    
    // If already throttled (from previous errors), suppress this log
    if (state.is_throttled) {
        state.throttled_count++;
        if (metrics_) {
            metrics_->increment("log.throttled." + subsystem);
        }
        return true;
    }
    
    return false;
}

void LogThrottler::record_success(const std::string& subsystem) {
    auto it = subsystem_states_.find(subsystem);
    if (it != subsystem_states_.end()) {
        auto& state = it->second;
        state.error_count = 0;
        state.is_throttled = false;
        state.just_activated = false;
        state.window_start = std::chrono::steady_clock::now();
    }
}

bool LogThrottler::was_just_activated(const std::string& subsystem) {
    auto it = subsystem_states_.find(subsystem);
    if (it != subsystem_states_.end()) {
        bool result = it->second.just_activated;
        it->second.just_activated = false; // Clear flag after reading
        return result;
    }
    return false;
}

int64_t LogThrottler::get_throttled_count(const std::string& subsystem) const {
    auto it = subsystem_states_.find(subsystem);
    if (it != subsystem_states_.end()) {
        return it->second.throttled_count;
    }
    return 0;
}

void LogThrottler::reset() {
    subsystem_states_.clear();
}

void LogThrottler::update_window(const std::string& subsystem) {
    auto& state = subsystem_states_[subsystem];
    auto now = std::chrono::steady_clock::now();
    
    // Initialize window start if needed
    if (state.window_start == std::chrono::steady_clock::time_point{}) {
        state.window_start = now;
        return;
    }
    
    // Check if window has expired
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - state.window_start).count();
    
    if (elapsed >= config_.window_seconds) {
        // Window expired - reset error count but keep throttled count
        state.error_count = 0;
        state.is_throttled = false;
        state.just_activated = false;
        state.window_start = now;
    }
}

}

