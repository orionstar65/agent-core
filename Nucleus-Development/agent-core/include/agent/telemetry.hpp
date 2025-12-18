#pragma once

#include <string>
#include <memory>
#include <map>
#include <cstdint>

namespace agent {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};

class Logger {
public:
    virtual ~Logger() = default;
    
    // Log structured message
    virtual void log(LogLevel level, 
                    const std::string& subsystem,
                    const std::string& message,
                    const std::map<std::string, std::string>& fields = {},
                    const std::string& deviceId = "",
                    const std::string& correlationId = "",
                    const std::string& eventId = "") = 0;
};

class Metrics {
public:
    virtual ~Metrics() = default;
    
    // Increment counter
    virtual void increment(const std::string& name, int64_t value = 1) = 0;
    
    // Record histogram value
    virtual void histogram(const std::string& name, double value) = 0;
    
    // Set gauge value
    virtual void gauge(const std::string& name, double value) = 0;
};

// Forward declaration
struct Config;

// Create logger implementation
std::unique_ptr<Logger> create_logger(const std::string& level, bool json);

// Create logger with throttling support
// Note: Config must be fully defined when calling this function
// We use a forward-declared struct to avoid circular dependencies
struct LoggingThrottleConfig {
    bool enabled;
    int error_threshold;
    int window_seconds;
};

std::unique_ptr<Logger> create_logger_with_throttle(
    const std::string& level, 
    bool json, 
    const LoggingThrottleConfig& throttle_config,
    Metrics* metrics = nullptr);

// Create metrics implementation
std::unique_ptr<Metrics> create_metrics();

}
