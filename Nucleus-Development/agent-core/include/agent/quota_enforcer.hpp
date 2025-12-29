#pragma once

#include "agent/config.hpp"
#include "agent/resource_monitor.hpp"
#include "agent/extension_manager.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

namespace agent {

enum class QuotaStage {
    Normal,
    Warn,
    Throttle,
    Stop
};

struct QuotaViolation {
    std::string resource_type;  // "CPU", "Memory", "Network"
    double usage_pct;            // Percentage of max limit
    QuotaStage stage;
    std::vector<std::string> offenders;  // Process names/PIDs
    std::chrono::system_clock::time_point timestamp;
};

struct ProcessEnforcementState {
    QuotaStage current_stage{QuotaStage::Normal};
    std::chrono::steady_clock::time_point last_violation_time;
    int violation_count{0};
};

class QuotaEnforcer {
public:
    QuotaEnforcer();
    ~QuotaEnforcer() = default;
    
    /// Evaluate aggregate resource usage and determine if quota is violated
    /// Returns QuotaViolation with stage Normal if no violation
    QuotaViolation evaluate(const Config& config,
                           ResourceMonitor* monitor,
                           ExtensionManager* ext_manager);
    
    /// Apply enforcement actions based on violation stage
    void enforce(const QuotaViolation& violation,
                ResourceMonitor* monitor,
                ExtensionManager* ext_manager,
                const Config& config);
    
    /// Reset enforcement for a specific process
    void reset_enforcement(const std::string& process_name);
    
    /// Reset all enforcement states
    void reset_all_enforcement();

private:
    // Track enforcement state per process
    std::map<std::string, ProcessEnforcementState> enforcement_states_;
    
    // Helper methods
    QuotaStage determine_stage(double usage_pct, 
                              double warn_threshold,
                              double throttle_threshold,
                              double stop_threshold) const;
    
    bool is_critical_extension(const std::string& name, const Config& config) const;
    
    std::vector<int> collect_all_pids(ExtensionManager* ext_manager, int agent_pid) const;
};

} // namespace agent

