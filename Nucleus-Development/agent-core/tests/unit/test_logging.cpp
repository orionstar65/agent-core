#include "agent/telemetry.hpp"
#include "agent/config.hpp"
#include <iostream>
#include <cassert>
#include <sstream>
#include <nlohmann/json.hpp>

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
    
    std::string get_output() {
        return buffer.str();
    }
    
    void clear() {
        buffer.str("");
        buffer.clear();
    }

private:
    std::ostringstream buffer;
    std::streambuf* old_buf;
};

void test_json_logging_fields() {
    std::cout << "\n=== Test: JSON Logging Required Fields ===\n";
    
    LogCapture capture;
    auto logger = create_logger("info", true);
    
    std::string deviceId = "test-device-123";
    std::string correlationId = "corr-abc-xyz";
    std::string eventId = "evt-001";
    
    logger->log(LogLevel::Info, "TestSubsystem", "Test message", 
                {{"key1", "value1"}}, deviceId, correlationId, eventId);
    
    std::string output = capture.get_output();
    
    // Find the JSON log line (skip initialization message)
    std::istringstream iss(output);
    std::string line;
    std::string json_line;
    while (std::getline(iss, line)) {
        if (line.find("Logger initialized") == std::string::npos && 
            line.find("{") != std::string::npos) {
            json_line = line;
            break;
        }
    }
    
    assert(!json_line.empty() && "Should have JSON log entry");
    
    // Parse JSON
    json log_entry = json::parse(json_line);
    
    // Verify all required fields are present
    assert(log_entry.contains("timestamp") && "timestamp field required");
    assert(log_entry.contains("level") && "level field required");
    assert(log_entry.contains("subsystem") && "subsystem field required");
    assert(log_entry.contains("deviceId") && "deviceId field required");
    assert(log_entry.contains("correlationId") && "correlationId field required");
    assert(log_entry.contains("eventId") && "eventId field required");
    assert(log_entry.contains("message") && "message field required");
    
    // Verify field values
    assert(log_entry["level"] == "INFO" && "level should be INFO");
    assert(log_entry["subsystem"] == "TestSubsystem" && "subsystem should match");
    assert(log_entry["deviceId"] == deviceId && "deviceId should match");
    assert(log_entry["correlationId"] == correlationId && "correlationId should match");
    assert(log_entry["eventId"] == eventId && "eventId should match");
    assert(log_entry["message"] == "Test message" && "message should match");
    
    // Verify additional fields
    assert(log_entry.contains("fields") && "fields object should exist");
    assert(log_entry["fields"]["key1"] == "value1" && "additional field should match");
    
    // Verify timestamp format (ISO 8601 with Z)
    std::string timestamp = log_entry["timestamp"];
    assert(timestamp.back() == 'Z' && "timestamp should end with Z");
    assert(timestamp.find('T') != std::string::npos && "timestamp should contain T");
    
    std::cout << "✓ All required fields present and correct\n";
}

void test_json_logging_optional_fields() {
    std::cout << "\n=== Test: JSON Logging Optional Fields ===\n";
    
    LogCapture capture;
    auto logger = create_logger("info", true);
    
    // Log without optional fields
    logger->log(LogLevel::Warn, "TestSubsystem", "Test without optional fields");
    
    std::string output = capture.get_output();
    
    // Find the JSON log line (skip initialization message)
    std::istringstream iss(output);
    std::string line;
    std::string json_line;
    while (std::getline(iss, line)) {
        if (line.find("Logger initialized") == std::string::npos && 
            line.find("{") != std::string::npos) {
            json_line = line;
            break;
        }
    }
    
    assert(!json_line.empty() && "Should have JSON log entry");
    json log_entry = json::parse(json_line);
    
    // Optional fields should be empty strings, not missing
    assert(log_entry["deviceId"] == "" && "deviceId should be empty string");
    assert(log_entry["correlationId"] == "" && "correlationId should be empty string");
    assert(log_entry["eventId"] == "" && "eventId should be empty string");
    
    std::cout << "✓ Optional fields default to empty strings\n";
}

void test_log_level_filtering() {
    std::cout << "\n=== Test: Log Level Filtering ===\n";
    
    LogCapture capture;
    auto logger = create_logger("warn", true);
    
    // These should be filtered out
    logger->log(LogLevel::Trace, "Test", "Trace message");
    logger->log(LogLevel::Debug, "Test", "Debug message");
    logger->log(LogLevel::Info, "Test", "Info message");
    
    std::string output = capture.get_output();
    assert(output.empty() && "Lower level logs should be filtered");
    
    // These should be logged
    logger->log(LogLevel::Warn, "Test", "Warn message");
    logger->log(LogLevel::Error, "Test", "Error message");
    
    output = capture.get_output();
    assert(!output.empty() && "Warn and above should be logged");
    
    // Count non-empty lines (excluding initialization message)
    size_t line_count = 0;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.find("Logger initialized") == std::string::npos) {
            line_count++;
        }
    }
    assert(line_count == 2 && "Should have 2 log entries");
    
    std::cout << "✓ Log level filtering works correctly\n";
}

void test_text_logging_format() {
    std::cout << "\n=== Test: Text Logging Format ===\n";
    
    LogCapture capture;
    auto logger = create_logger("info", false);
    
    logger->log(LogLevel::Info, "TestSubsystem", "Test message",
                {{"key1", "value1"}}, "device-123", "corr-456", "evt-789");
    
    std::string output = capture.get_output();
    
    // Check for required components in text format
    assert(output.find("[INFO]") != std::string::npos && "Should contain level");
    assert(output.find("[TestSubsystem]") != std::string::npos && "Should contain subsystem");
    assert(output.find("deviceId=device-123") != std::string::npos && "Should contain deviceId");
    assert(output.find("correlationId=corr-456") != std::string::npos && "Should contain correlationId");
    assert(output.find("eventId=evt-789") != std::string::npos && "Should contain eventId");
    assert(output.find("Test message") != std::string::npos && "Should contain message");
    
    std::cout << "✓ Text logging format is correct\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Structured Logging Unit Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_json_logging_fields();
        test_json_logging_optional_fields();
        test_log_level_filtering();
        test_text_logging_format();
        
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

