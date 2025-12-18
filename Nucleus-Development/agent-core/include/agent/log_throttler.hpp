#pragma once

#include <string>
#include <map>
#include <chrono>
#include <memory>
#include "config.hpp"
#include "telemetry.hpp"

namespace agent {

class LogThrottler {
public:
    explicit LogThrottler(const Config::Logging::Throttle& config, Metrics* metrics = nullptr);
    
    // Check if a log should be throttled (suppressed)
    // Returns true if log should be suppressed, false if it should be logged
    bool should_throttle(LogLevel level, const std::string& subsystem);
    
    // Record a successful operation (resets throttling for that subsystem)
    void record_success(const std::string& subsystem);
    
    // Get throttled count for a subsystem
    int64_t get_throttled_count(const std::string& subsystem) const;
    
    // Check if throttling was just activated (for emitting activation message)
    bool was_just_activated(const std::string& subsystem);
    
    // Reset all throttling state
    void reset();

private:
    struct SubsystemState {
        int error_count{0};
        int64_t throttled_count{0};
        std::chrono::steady_clock::time_point window_start;
        bool is_throttled{false};
        bool just_activated{false};
    };
    
    const Config::Logging::Throttle config_;
    Metrics* metrics_;
    mutable std::map<std::string, SubsystemState> subsystem_states_;
    
    void update_window(const std::string& subsystem);
};

}

