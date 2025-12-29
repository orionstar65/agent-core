#include "agent/quota_enforcer.hpp"
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace agent {

QuotaEnforcer::QuotaEnforcer() {
}

QuotaViolation QuotaEnforcer::evaluate(const Config& config,
                                       ResourceMonitor* monitor,
                                       ExtensionManager* ext_manager) {
    QuotaViolation violation;
    violation.stage = QuotaStage::Normal;
    violation.timestamp = std::chrono::system_clock::now();
    
    if (!monitor || !ext_manager) {
        return violation;
    }
    
    // Collect all PIDs (agent-core + extensions)
    int agent_pid = 
#ifdef _WIN32
        GetCurrentProcessId();
#else
        getpid();
#endif
    std::vector<int> all_pids = collect_all_pids(ext_manager, agent_pid);
    
    // Aggregate resource usage
    ResourceUsage aggregate = monitor->aggregate_usage(all_pids);
    
    // Evaluate each resource type
    double cpu_usage_pct = (aggregate.cpu_pct / config.resource.cpu_max_pct) * 100.0;
    double mem_usage_pct = (static_cast<double>(aggregate.mem_mb) / config.resource.mem_max_mb) * 100.0;
    int64_t total_net = aggregate.net_in_kbps + aggregate.net_out_kbps;
    double net_usage_pct = (static_cast<double>(total_net) / config.resource.net_max_kbps) * 100.0;
    
    // Determine which resource has the highest violation
    double max_usage = std::max({cpu_usage_pct, mem_usage_pct, net_usage_pct});
    
    if (max_usage >= config.resource.stop_threshold_pct) {
        violation.stage = QuotaStage::Stop;
        violation.usage_pct = max_usage;
    } else if (max_usage >= config.resource.throttle_threshold_pct) {
        violation.stage = QuotaStage::Throttle;
        violation.usage_pct = max_usage;
    } else if (max_usage >= config.resource.warn_threshold_pct) {
        violation.stage = QuotaStage::Warn;
        violation.usage_pct = max_usage;
    } else {
        return violation; // Normal - no violation
    }
    
    // Determine which resource type is the offender
    if (max_usage == cpu_usage_pct) {
        violation.resource_type = "CPU";
    } else if (max_usage == mem_usage_pct) {
        violation.resource_type = "Memory";
    } else {
        violation.resource_type = "Network";
    }
    
    // Identify offending processes
    // Find processes contributing most to the violation
    for (int pid : all_pids) {
        if (pid <= 0) continue;
        
        auto usage = monitor->sample_by_pid(pid);
        bool is_offender = false;
        
        if (violation.resource_type == "CPU" && usage.cpu_pct > 0) {
            double pid_cpu_pct = (usage.cpu_pct / config.resource.cpu_max_pct) * 100.0;
            if (pid_cpu_pct >= config.resource.warn_threshold_pct) {
                is_offender = true;
            }
        } else if (violation.resource_type == "Memory" && usage.mem_mb > 0) {
            double pid_mem_pct = (static_cast<double>(usage.mem_mb) / config.resource.mem_max_mb) * 100.0;
            if (pid_mem_pct >= config.resource.warn_threshold_pct) {
                is_offender = true;
            }
        } else if (violation.resource_type == "Network") {
            int64_t pid_net = usage.net_in_kbps + usage.net_out_kbps;
            double pid_net_pct = (static_cast<double>(pid_net) / config.resource.net_max_kbps) * 100.0;
            if (pid_net_pct >= config.resource.warn_threshold_pct) {
                is_offender = true;
            }
        }
        
        if (is_offender) {
            // Try to get process name from extension manager
            auto process_info = ext_manager->get_process_info();
            std::string process_name = "pid:" + std::to_string(pid);
            
            for (const auto& [name, info] : process_info) {
                if (info.pid == pid) {
                    process_name = name;
                    break;
                }
            }
            
            if (pid == agent_pid) {
                process_name = "agent-core";
            }
            
            violation.offenders.push_back(process_name);
        }
    }
    
    return violation;
}

void QuotaEnforcer::enforce(const QuotaViolation& violation,
                            ResourceMonitor* monitor,
                            ExtensionManager* ext_manager,
                            const Config& config) {
    if (!monitor || !ext_manager || violation.stage == QuotaStage::Normal) {
        return;
    }
    
    // Get process info for all extensions
    auto process_info = ext_manager->get_process_info();
    int agent_pid = 
#ifdef _WIN32
        GetCurrentProcessId();
#else
        getpid();
#endif
    
    // Apply enforcement actions based on stage
    for (const std::string& offender : violation.offenders) {
        int pid = 0;
        std::string ext_name;
        
        if (offender == "agent-core") {
            pid = agent_pid;
            ext_name = "agent-core";
        } else {
            // Find PID from extension name
            for (const auto& [name, info] : process_info) {
                if (name == offender) {
                    pid = info.pid;
                    ext_name = name;
                    break;
                }
            }
            
            // Try parsing as "pid:XXXX"
            if (pid == 0 && offender.find("pid:") == 0) {
                try {
                    pid = std::stoi(offender.substr(4));
                } catch (...) {
                    continue;
                }
            }
        }
        
        if (pid <= 0) continue;
        
        // Check if this is a critical extension
        bool is_critical = is_critical_extension(ext_name, config);
        
        // Get current enforcement state
        auto& state = enforcement_states_[ext_name];
        state.last_violation_time = std::chrono::steady_clock::now();
        state.violation_count++;
        
        if (violation.stage == QuotaStage::Stop) {
            if (!is_critical && ext_name != "agent-core") {
                // Stop non-critical extensions
                ext_manager->stop(ext_name);
                state.current_stage = QuotaStage::Stop;
            } else {
                // For critical extensions or agent-core, apply maximum throttling
                monitor->set_cpu_priority(pid, 2); // Idle priority
                if (violation.resource_type == "Memory") {
                    // Set memory limit to 90% of max for this process
                    int64_t process_limit = (config.resource.mem_max_mb * 90) / 100;
                    monitor->set_memory_limit(pid, process_limit);
                }
                state.current_stage = QuotaStage::Throttle;
            }
        } else if (violation.stage == QuotaStage::Throttle) {
            // Apply throttling
            monitor->set_cpu_priority(pid, 1); // Below normal priority
            if (violation.resource_type == "Memory") {
                // Set memory limit to 95% of max for this process
                int64_t process_limit = (config.resource.mem_max_mb * 95) / 100;
                monitor->set_memory_limit(pid, process_limit);
            }
            state.current_stage = QuotaStage::Throttle;
        } else if (violation.stage == QuotaStage::Warn) {
            // Just log warning, no enforcement action
            state.current_stage = QuotaStage::Warn;
        }
    }
    
    // Reset enforcement for processes not in violation
    auto all_processes = ext_manager->get_process_info();
    for (const auto& [name, info] : all_processes) {
        if (std::find(violation.offenders.begin(), violation.offenders.end(), name) 
            == violation.offenders.end()) {
            // Process is not an offender, reset if it was previously throttled
            auto it = enforcement_states_.find(name);
            if (it != enforcement_states_.end() && 
                it->second.current_stage != QuotaStage::Normal) {
                reset_enforcement(name);
            }
        }
    }
}

void QuotaEnforcer::reset_enforcement(const std::string& process_name) {
    auto it = enforcement_states_.find(process_name);
    if (it != enforcement_states_.end()) {
        it->second.current_stage = QuotaStage::Normal;
        it->second.violation_count = 0;
    }
}

void QuotaEnforcer::reset_all_enforcement() {
    for (auto& [name, state] : enforcement_states_) {
        state.current_stage = QuotaStage::Normal;
        state.violation_count = 0;
    }
}

QuotaStage QuotaEnforcer::determine_stage(double usage_pct,
                                          double warn_threshold,
                                          double throttle_threshold,
                                          double stop_threshold) const {
    if (usage_pct >= stop_threshold) {
        return QuotaStage::Stop;
    } else if (usage_pct >= throttle_threshold) {
        return QuotaStage::Throttle;
    } else if (usage_pct >= warn_threshold) {
        return QuotaStage::Warn;
    }
    return QuotaStage::Normal;
}

bool QuotaEnforcer::is_critical_extension(const std::string& name, const Config& config) const {
    if (name == "agent-core") {
        return true; // Agent-core is always critical
    }
    
    for (const std::string& critical : config.resource.critical_extensions) {
        if (critical == name) {
            return true;
        }
    }
    
    return false;
}

std::vector<int> QuotaEnforcer::collect_all_pids(ExtensionManager* ext_manager, int agent_pid) const {
    std::vector<int> pids;
    pids.push_back(agent_pid);
    
    if (ext_manager) {
        auto process_info = ext_manager->get_process_info();
        for (const auto& [name, info] : process_info) {
            if (info.pid > 0) {
                pids.push_back(info.pid);
            }
        }
    }
    
    return pids;
}

} // namespace agent

