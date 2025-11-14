#include "agent/telemetry.hpp"
#include <iostream>
#include <ctime>
#include <iomanip>

namespace agent {

class LoggerImpl : public Logger {
public:
    LoggerImpl(const std::string& level, bool json) 
        : min_level_(parse_level(level)), use_json_(json) {
        std::cout << "Logger initialized: level=" << level 
                  << ", json=" << (json ? "true" : "false") << "\n";
    }
    
    void log(LogLevel level, 
             const std::string& subsystem,
             const std::string& message,
             const std::map<std::string, std::string>& fields) override {
        
        if (level < min_level_) {
            return;
        }
        
        if (use_json_) {
            log_json(level, subsystem, message, fields);
        } else {
            log_text(level, subsystem, message, fields);
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
                  const std::map<std::string, std::string>& fields) {
        // TODO: Use proper JSON library
        std::cout << "{\"timestamp\":\"" << get_timestamp() << "\""
                  << ",\"level\":\"" << level_string(level) << "\""
                  << ",\"subsystem\":\"" << subsystem << "\""
                  << ",\"message\":\"" << message << "\"";
        
        for (const auto& [key, value] : fields) {
            std::cout << ",\"" << key << "\":\"" << value << "\"";
        }
        
        std::cout << "}\n";
    }
    
    void log_text(LogLevel level,
                  const std::string& subsystem,
                  const std::string& message,
                  const std::map<std::string, std::string>& fields) {
        std::cout << "[" << get_timestamp() << "] "
                  << "[" << level_string(level) << "] "
                  << "[" << subsystem << "] "
                  << message;
        
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
        auto now = std::time(nullptr);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
};

std::unique_ptr<Logger> create_logger(const std::string& level, bool json) {
    return std::make_unique<LoggerImpl>(level, json);
}

}
