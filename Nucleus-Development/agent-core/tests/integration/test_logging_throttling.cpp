#include "agent/telemetry.hpp"
#include "agent/config.hpp"
#include "agent/log_throttler.hpp"
#include <iostream>
#include <cassert>
#include <sstream>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

using namespace agent;
using json = nlohmann::json;

// Capture stdout for testing
class LogCapture {
public:
    LogCapture() {
        old_buf = std::cout.rdbuf();
        std::cout.rdbuf(buffer.rdbuf());
    }
    
    ~LogCapture() {
        std::cout.rdbuf(old_buf);
    }
    
    std::vector<std::string> get_lines() {
        std::vector<std::string> lines;
        std::istringstream iss(buffer.str());
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.find("Logger initialized") == std::string::npos) {
                lines.push_back(line);
            }
        }
        return lines;
    }
    
    void clear() {
        buffer.str("");
        buffer.clear();
    }

private:
    std::ostringstream buffer;
    std::streambuf* old_buf;
};

void test_throttled_logger_integration() {
    std::cout << "\n=== Test: Throttled Logger Integration ===\n";
    
    LogCapture capture;
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 5;
    throttle_config.window_seconds = 60;
    
    auto metrics = create_metrics();
    LoggingThrottleConfig throttle_cfg;
    throttle_cfg.enabled = throttle_config.enabled;
    throttle_cfg.error_threshold = throttle_config.error_threshold;
    throttle_cfg.window_seconds = throttle_config.window_seconds;
    auto logger = create_logger_with_throttle("info", true, throttle_cfg, metrics.get());
    
    std::string deviceId = "test-device";
    std::string correlationId = "test-corr-123";
    
    // Generate 10 errors - first 5 should be logged, next 5 should be throttled
    for (int i = 0; i < 10; i++) {
        logger->log(LogLevel::Error, "Auth", "Authentication failed", 
                    {{"attempt", std::to_string(i+1)}}, deviceId, correlationId);
    }
    
    auto lines = capture.get_lines();
    
    // Should have: 5 error logs + 1 activation message
    int error_count = 0;
    int activation_count = 0;
    for (const auto& line : lines) {
        try {
            json log_entry = json::parse(line);
            if (log_entry["level"] == "ERROR") {
                error_count++;
            } else if (log_entry["level"] == "WARN" && 
                      log_entry["message"].get<std::string>().find("throttling activated") != std::string::npos) {
                activation_count++;
            }
        } catch (...) {
            // Skip non-JSON lines
        }
    }
    
    assert(error_count == 5 && "Should have logged 5 errors before throttling");
    assert(activation_count == 1 && "Should have activation message");
    
    std::cout << "✓ Throttled logger suppresses repetitive errors\n";
}

void test_correlation_id_propagation() {
    std::cout << "\n=== Test: Correlation ID Propagation ===\n";
    
    LogCapture capture;
    auto logger = create_logger("info", true);
    
    std::string correlationId = "corr-test-456";
    std::string deviceId = "device-789";
    
    // Log multiple messages with same correlation ID
    logger->log(LogLevel::Info, "Auth", "Starting authentication", {}, deviceId, correlationId);
    logger->log(LogLevel::Info, "Auth", "Validating certificate", {}, deviceId, correlationId);
    logger->log(LogLevel::Info, "Auth", "Authentication complete", {}, deviceId, correlationId);
    
    auto lines = capture.get_lines();
    
    // Verify all logs have the same correlation ID
    for (const auto& line : lines) {
        try {
            json log_entry = json::parse(line);
            assert(log_entry["correlationId"] == correlationId && 
                   "All logs should have same correlation ID");
            assert(log_entry["deviceId"] == deviceId && 
                   "All logs should have same device ID");
        } catch (...) {
            // Skip non-JSON lines
        }
    }
    
    std::cout << "✓ Correlation ID propagates through operation chain\n";
}

void test_throttling_summary_emission() {
    std::cout << "\n=== Test: Throttling Summary Emission ===\n";
    
    LogCapture capture;
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 3;
    throttle_config.window_seconds = 60;
    
    auto metrics = create_metrics();
    LoggingThrottleConfig throttle_cfg;
    throttle_cfg.enabled = throttle_config.enabled;
    throttle_cfg.error_threshold = throttle_config.error_threshold;
    throttle_cfg.window_seconds = throttle_config.window_seconds;
    auto logger = create_logger_with_throttle("info", true, throttle_cfg, metrics.get());
    
    // Trigger throttling
    for (int i = 0; i < 5; i++) {
        logger->log(LogLevel::Error, "Network", "Connection failed", {}, "device-1");
    }
    
    capture.clear();
    
    // Emit a non-error log - should trigger summary
    logger->log(LogLevel::Info, "Network", "Connection restored", {}, "device-1");
    
    auto lines = capture.get_lines();
    
    // Should have summary message
    bool found_summary = false;
    for (const auto& line : lines) {
        try {
            json log_entry = json::parse(line);
            std::string message = log_entry["message"];
            if (message.find("Throttling summary") != std::string::npos) {
                found_summary = true;
                assert(log_entry["fields"].contains("throttledCount") && 
                       "Summary should contain throttled count");
            }
        } catch (...) {
            // Skip non-JSON lines
        }
    }
    
    assert(found_summary && "Should emit throttling summary");
    
    std::cout << "✓ Throttling summary emitted on recovery\n";
}

void test_offline_scenario_simulation() {
    std::cout << "\n=== Test: Offline Scenario Simulation ===\n";
    
    LogCapture capture;
    
    Config::Logging::Throttle throttle_config;
    throttle_config.enabled = true;
    throttle_config.error_threshold = 5;
    throttle_config.window_seconds = 60;
    
    auto metrics = create_metrics();
    LoggingThrottleConfig throttle_cfg;
    throttle_cfg.enabled = throttle_config.enabled;
    throttle_cfg.error_threshold = throttle_config.error_threshold;
    throttle_cfg.window_seconds = throttle_config.window_seconds;
    auto logger = create_logger_with_throttle("info", true, throttle_cfg, metrics.get());
    
    std::string deviceId = "offline-device";
    std::string correlationId = "offline-op-001";
    
    // Simulate offline scenario: many network errors
    for (int i = 0; i < 20; i++) {
        logger->log(LogLevel::Error, "Network", "Network unreachable", 
                   {{"retry", std::to_string(i)}}, deviceId, correlationId);
    }
    
    auto lines = capture.get_lines();
    
    // Count actual error logs (should be limited by throttling)
    int error_logs = 0;
    for (const auto& line : lines) {
        try {
            json log_entry = json::parse(line);
            if (log_entry["level"] == "ERROR") {
                error_logs++;
            }
        } catch (...) {
            // Skip non-JSON lines
        }
    }
    
    // Should have logged threshold errors + activation, not all 20
    assert(error_logs <= throttle_config.error_threshold + 1 && 
           "Should not log all errors when throttled");
    
    std::cout << "✓ Offline scenario throttles repetitive errors\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Logging & Throttling Integration Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_throttled_logger_integration();
        test_correlation_id_propagation();
        test_throttling_summary_emission();
        test_offline_scenario_simulation();
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    } catch (...) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with unknown exception\n";
        std::cerr << "========================================\n";
        return 1;
    }
}

