#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include "config.hpp"
#include "resource_monitor.hpp"
#include "extension_manager.hpp"
#include "telemetry.hpp"
#include "quota_enforcer.hpp"

namespace agent {

struct TelemetryReading {
    std::string component;
    std::string name;
    double value;
};

struct TelemetryBatch {
    std::string date_time;
    std::vector<TelemetryReading> readings;
};

struct QuotaEvent {
    std::string resource_type;
    double usage_pct;
    std::string stage;  // "warn", "throttle", "stop"
    std::vector<std::string> offenders;
    std::chrono::system_clock::time_point timestamp;
};

class TelemetryCollector {
public:
    TelemetryCollector(ResourceMonitor* resource_monitor,
                      ExtensionManager* extension_manager,
                      Logger* logger,
                      Metrics* metrics,
                      const Config& config);
    
    // Collect a batch of telemetry readings
    TelemetryBatch collect();
    
    // Check alert thresholds and log warnings
    void check_alerts(const TelemetryBatch& batch);
    
    // Convert batch to JSON string
    std::string to_json(const TelemetryBatch& batch) const;
    
    // Add quota event to batch
    void add_quota_event(TelemetryBatch& batch, const QuotaViolation& violation) const;
    
    // Convert quota event to JSON string (for immediate publishing)
    std::string quota_event_to_json(const QuotaViolation& violation) const;

private:
    ResourceMonitor* resource_monitor_;
    ExtensionManager* extension_manager_;
    Logger* logger_;
    Metrics* metrics_;
    const Config& config_;
    
    std::string get_current_datetime() const;
    std::string get_executable_name() const;
    int get_current_pid() const;
    void add_reading(TelemetryBatch& batch, const std::string& component, 
                     const std::string& name, double value);
};

}

