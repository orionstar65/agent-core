#include "agent/telemetry_collector.hpp"
#include "agent/quota_enforcer.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

using json = nlohmann::json;

namespace agent {

TelemetryCollector::TelemetryCollector(ResourceMonitor* resource_monitor,
                                       ExtensionManager* extension_manager,
                                       Logger* logger,
                                       Metrics* metrics,
                                       const Config& config)
    : resource_monitor_(resource_monitor),
      extension_manager_(extension_manager),
      logger_(logger),
      metrics_(metrics),
      config_(config) {
}

TelemetryBatch TelemetryCollector::collect() {
    TelemetryBatch batch;
    batch.date_time = get_current_datetime();
    
    // Collect system-wide metrics
    auto system_usage = resource_monitor_->sample_system();
    add_reading(batch, "System", "CPU", system_usage.cpu_pct);
    add_reading(batch, "System", "Memory", static_cast<double>(system_usage.mem_mb));
    add_reading(batch, "System", "Network out", static_cast<double>(system_usage.net_out_kbps));
    add_reading(batch, "System", "Network in", static_cast<double>(system_usage.net_in_kbps));
    
    // Collect agent-core metrics
    int core_pid = get_current_pid();
    auto core_usage = resource_monitor_->sample_by_pid(core_pid);
    std::string core_name = get_executable_name();
    add_reading(batch, core_name, "CPU", core_usage.cpu_pct);
    add_reading(batch, core_name, "Memory", static_cast<double>(core_usage.mem_mb));
    if (core_usage.handles > 0) {
        add_reading(batch, core_name, "Handles", static_cast<double>(core_usage.handles));
    }
    
    // Collect extension metrics
    auto process_info = extension_manager_->get_process_info();
    for (const auto& [name, info] : process_info) {
        if (info.pid > 0) {
            auto ext_usage = resource_monitor_->sample_by_pid(info.pid);
            std::string component_name = info.executable_name.empty() ? name : info.executable_name;
            add_reading(batch, component_name, "CPU", ext_usage.cpu_pct);
            add_reading(batch, component_name, "Memory", static_cast<double>(ext_usage.mem_mb));
            if (ext_usage.handles > 0) {
                add_reading(batch, component_name, "Handles", static_cast<double>(ext_usage.handles));
            }
        }
    }
    
    if (metrics_) {
        metrics_->increment("telemetry.readings_collected", batch.readings.size());
    }
    
    return batch;
}

void TelemetryCollector::check_alerts(const TelemetryBatch& batch) {
    for (const auto& reading : batch.readings) {
        bool is_warn = false;
        bool is_critical = false;
        
        if (reading.name == "CPU") {
            if (reading.value >= config_.telemetry.alerts.cpu_critical_pct) {
                is_critical = true;
            } else if (reading.value >= config_.telemetry.alerts.cpu_warn_pct) {
                is_warn = true;
            }
        } else if (reading.name == "Memory") {
            if (reading.value >= config_.telemetry.alerts.mem_critical_mb) {
                is_critical = true;
            } else if (reading.value >= config_.telemetry.alerts.mem_warn_mb) {
                is_warn = true;
            }
        } else if (reading.name == "Network out" || reading.name == "Network in") {
            if (reading.value >= config_.telemetry.alerts.net_critical_kbps) {
                is_critical = true;
            } else if (reading.value >= config_.telemetry.alerts.net_warn_kbps) {
                is_warn = true;
            }
        }
        
        if (is_critical && logger_) {
            logger_->log(LogLevel::Error, "Telemetry", 
                        "Critical threshold exceeded: " + reading.component + 
                        " " + reading.name + " = " + std::to_string(reading.value));
            if (metrics_) {
                metrics_->increment("telemetry.alerts.critical");
            }
        } else if (is_warn && logger_) {
            logger_->log(LogLevel::Warn, "Telemetry",
                        "Warning threshold exceeded: " + reading.component +
                        " " + reading.name + " = " + std::to_string(reading.value));
            if (metrics_) {
                metrics_->increment("telemetry.alerts.warn");
            }
        }
    }
}

std::string TelemetryCollector::to_json(const TelemetryBatch& batch) const {
    json j;
    j["DateTime"] = batch.date_time;
    
    json readings_array = json::array();
    for (const auto& reading : batch.readings) {
        json r;
        r["Component"] = reading.component;
        r["Name"] = reading.name;
        r["Value"] = reading.value;
        readings_array.push_back(r);
    }
    j["Readings"] = readings_array;
    
    return j.dump();
}

std::string TelemetryCollector::get_current_datetime() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%m/%d/%Y %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string TelemetryCollector::get_executable_name() const {
#ifdef _WIN32
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) != 0) {
        std::string path(exe_path);
        size_t pos = path.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string name = path.substr(pos + 1);
            // Remove .exe extension
            if (name.size() > 4 && name.substr(name.size() - 4) == ".exe") {
                return name.substr(0, name.size() - 4);
            }
            return name;
        }
        return path;
    }
    return "agent-core";
#else
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string path(exe_path);
        size_t pos = path.find_last_of("/");
        if (pos != std::string::npos) {
            return path.substr(pos + 1);
        }
        return path;
    }
    return "agent-core";
#endif
}

int TelemetryCollector::get_current_pid() const {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

void TelemetryCollector::add_reading(TelemetryBatch& batch, const std::string& component,
                                     const std::string& name, double value) {
    TelemetryReading reading;
    reading.component = component;
    reading.name = name;
    reading.value = value;
    batch.readings.push_back(reading);
}

void TelemetryCollector::add_quota_event(TelemetryBatch& batch, const QuotaViolation& violation) const {
    // Add quota event as a special reading
    std::string stage_str;
    switch (violation.stage) {
        case QuotaStage::Warn:
            stage_str = "warn";
            break;
        case QuotaStage::Throttle:
            stage_str = "throttle";
            break;
        case QuotaStage::Stop:
            stage_str = "stop";
            break;
        default:
            return; // Don't add normal stage events
    }
    
    // Add as a reading with special component name
    TelemetryReading reading1;
    reading1.component = "Quota";
    reading1.name = violation.resource_type + "_" + stage_str;
    reading1.value = violation.usage_pct;
    batch.readings.push_back(reading1);
    
    // Also add offenders count
    TelemetryReading reading2;
    reading2.component = "Quota";
    reading2.name = violation.resource_type + "_offenders";
    reading2.value = static_cast<double>(violation.offenders.size());
    batch.readings.push_back(reading2);
}

std::string TelemetryCollector::quota_event_to_json(const QuotaViolation& violation) const {
    json j;
    
    std::string stage_str;
    switch (violation.stage) {
        case QuotaStage::Warn:
            stage_str = "warn";
            break;
        case QuotaStage::Throttle:
            stage_str = "throttle";
            break;
        case QuotaStage::Stop:
            stage_str = "stop";
            break;
        default:
            stage_str = "normal";
    }
    
    j["eventType"] = "quota_violation";
    j["resourceType"] = violation.resource_type;
    j["usagePercent"] = violation.usage_pct;
    j["stage"] = stage_str;
    j["offenders"] = violation.offenders;
    
    auto time_t = std::chrono::system_clock::to_time_t(violation.timestamp);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    j["timestamp"] = oss.str();
    
    return j.dump();
}

}

