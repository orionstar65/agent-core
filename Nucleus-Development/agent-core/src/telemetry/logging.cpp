#include "agent/telemetry.hpp"
#include "agent/log_throttler.hpp"
#include "agent/config.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <memory>

using json = nlohmann::json;

namespace agent {

class LoggerImpl : public Logger {
public:
    LoggerImpl(const std::string& level, bool json) 
        : min_level_(parse_level(level)), use_json_(json) {
    }
    
    void log(LogLevel level, 
             const std::string& subsystem,
             const std::string& message,
             const std::map<std::string, std::string>& fields,
             const std::string& deviceId,
             const std::string& correlationId,
             const std::string& eventId) override {
        
        if (level < min_level_) {
            return;
        }
        
        if (use_json_) {
            log_json(level, subsystem, message, fields, deviceId, correlationId, eventId);
        } else {
            log_text(level, subsystem, message, fields, deviceId, correlationId, eventId);
        }
    }

private:
    LogLevel min_level_;
    bool use_json_;
    
    LogLevel parse_level(const std::string& level) {
        if (level == "trace") return LogLevel::Trace;
        if (level == "debug") return LogLevel::Debug;
        if (level == "info") return LogLevel::Info;
        if (level == "warn") return LogLevel::Warn;
        if (level == "error") return LogLevel::Error;
        if (level == "critical") return LogLevel::Critical;
        return LogLevel::Info;
    }
    
    const char* level_string(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info: return "INFO";
            case LogLevel::Warn: return "WARN";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Critical: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
    
    void log_json(LogLevel level,
                  const std::string& subsystem,
                  const std::string& message,
                  const std::map<std::string, std::string>& fields,
                  const std::string& deviceId,
                  const std::string& correlationId,
                  const std::string& eventId) {
        json log_entry;
        
        // Required fields
        log_entry["timestamp"] = get_timestamp();
        log_entry["level"] = level_string(level);
        log_entry["subsystem"] = subsystem;
        log_entry["deviceId"] = deviceId.empty() ? "" : deviceId;
        log_entry["correlationId"] = correlationId.empty() ? "" : correlationId;
        log_entry["eventId"] = eventId.empty() ? "" : eventId;
        log_entry["message"] = message;
        
        // Additional fields as nested object
        if (!fields.empty()) {
            json fields_obj;
            for (const auto& [key, value] : fields) {
                fields_obj[key] = value;
            }
            log_entry["fields"] = fields_obj;
        }
        
        std::cout << log_entry.dump() << "\n";
    }
    
    void log_text(LogLevel level,
                  const std::string& subsystem,
                  const std::string& message,
                  const std::map<std::string, std::string>& fields,
                  const std::string& deviceId,
                  const std::string& correlationId,
                  const std::string& eventId) {
        std::cout << "[" << get_timestamp() << "] "
                  << "[" << level_string(level) << "] "
                  << "[" << subsystem << "] ";
        
        if (!deviceId.empty()) {
            std::cout << "[deviceId=" << deviceId << "] ";
        }
        if (!correlationId.empty()) {
            std::cout << "[correlationId=" << correlationId << "] ";
        }
        if (!eventId.empty()) {
            std::cout << "[eventId=" << eventId << "] ";
        }
        
        std::cout << message;
        
        if (!fields.empty()) {
            std::cout << " {";
            bool first = true;
            for (const auto& [key, value] : fields) {
                if (!first) std::cout << ", ";
                std::cout << key << "=" << value;
                first = false;
            }
            std::cout << "}";
        }
        
        std::cout << "\n";
    }
    
    std::string get_timestamp() {
        // Get current time with milliseconds precision in UTC
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &time_t);
#else
        gmtime_r(&time_t, &tm);
#endif
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        
        return oss.str();
    }
};

// Throttled logger wrapper
class ThrottledLogger : public Logger {
public:
    ThrottledLogger(std::unique_ptr<Logger> base_logger, 
                    std::unique_ptr<LogThrottler> throttler)
        : base_logger_(std::move(base_logger)), throttler_(std::move(throttler)) {
    }
    
    void log(LogLevel level, 
             const std::string& subsystem,
             const std::string& message,
             const std::map<std::string, std::string>& fields = {},
             const std::string& deviceId = "",
             const std::string& correlationId = "",
             const std::string& eventId = "") override {
        
        // Check if throttling was just activated - emit activation message
        if (throttler_ && throttler_->was_just_activated(subsystem)) {
            std::map<std::string, std::string> activation_fields = fields;
            base_logger_->log(LogLevel::Warn, subsystem, 
                             "Error throttling activated - subsequent errors will be suppressed",
                             activation_fields, deviceId, correlationId, eventId);
        }
        
        // Check if this log should be throttled
        if (throttler_ && throttler_->should_throttle(level, subsystem)) {
            // Log is throttled - don't emit it
            return;
        }
        
        // Emit throttling summary if needed (when throttling deactivates)
        int64_t throttled = throttler_ ? throttler_->get_throttled_count(subsystem) : 0;
        if (throttled > 0 && (level == LogLevel::Info || level == LogLevel::Warn || level == LogLevel::Debug)) {
            // Emit summary on first non-error log after throttling
            std::map<std::string, std::string> summary_fields = fields;
            summary_fields["throttledCount"] = std::to_string(throttled);
            base_logger_->log(LogLevel::Info, subsystem, 
                             "Throttling summary: " + std::to_string(throttled) + " errors suppressed",
                             summary_fields, deviceId, correlationId, eventId);
            // Reset throttled count after emitting summary
            if (throttler_) {
                throttler_->record_success(subsystem);
            }
        }
        
        // Forward to base logger
        base_logger_->log(level, subsystem, message, fields, deviceId, correlationId, eventId);
    }
    
    void record_success(const std::string& subsystem) {
        if (throttler_) {
            throttler_->record_success(subsystem);
        }
    }

private:
    std::unique_ptr<Logger> base_logger_;
    std::unique_ptr<LogThrottler> throttler_;
};

std::unique_ptr<Logger> create_logger(const std::string& level, bool json) {
    return std::make_unique<LoggerImpl>(level, json);
}

std::unique_ptr<Logger> create_logger_with_throttle(
    const std::string& level, 
    bool json, 
    const LoggingThrottleConfig& throttle_config,
    Metrics* metrics) {
    
    Config::Logging::Throttle config_throttle;
    config_throttle.enabled = throttle_config.enabled;
    config_throttle.error_threshold = throttle_config.error_threshold;
    config_throttle.window_seconds = throttle_config.window_seconds;
    
    auto base_logger = std::make_unique<LoggerImpl>(level, json);
    auto throttler = std::make_unique<LogThrottler>(config_throttle, metrics);
    return std::make_unique<ThrottledLogger>(std::move(base_logger), std::move(throttler));
}

}
