#pragma once

#include <string>
#include <cstdint>
#include <map>
#include <memory>
#include <cstdint>
#include "config.hpp"

namespace agent {

struct ResourceUsage {
    double cpu_pct{0.0};
    int64_t mem_mb{0};
    int64_t net_in_kbps{0};
    int64_t net_out_kbps{0};
    int64_t disk_read_mb{0};
    int64_t disk_write_mb{0};
    int64_t handles{0};
};

class ResourceMonitor {
public:
    virtual ~ResourceMonitor() = default;
    
    // Sample resource usage for a process by name
    virtual ResourceUsage sample(const std::string& process_name) const = 0;
    
    // Sample resource usage for a process by PID
    virtual ResourceUsage sample_by_pid(int pid) const = 0;
    
    // Sample system-wide resource usage
    virtual ResourceUsage sample_system() const = 0;
    
    // Check if usage exceeds budgets
    virtual bool exceeds_budget(const ResourceUsage& usage, const Config& config) const = 0;
};

// create default implementation
std::unique_ptr<ResourceMonitor> create_resource_monitor();

}
