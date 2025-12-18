#include "agent/resource_monitor.hpp"
#include <iostream>

namespace agent {

class ResourceMonitorImpl : public ResourceMonitor {
public:
    ResourceUsage sample(const std::string&) const override {
        ResourceUsage usage;
        
        // TODO: Implement actual resource sampling
        // for now return stub values
        usage.cpu_pct = 5.0;
        usage.mem_mb = 50;
        usage.net_kbps = 10;
        
        return usage;
    }
    
    bool exceeds_budget(const ResourceUsage& usage, const Config& config) const override {
        bool exceeds = false;
        
        if (usage.cpu_pct > config.resource.cpu_max_pct) {
            std::cout << "ResourceMonitor: CPU exceeds budget (" 
                      << usage.cpu_pct << "% > " << config.resource.cpu_max_pct << "%)\n";
            exceeds = true;
        }
        
        if (usage.mem_mb > config.resource.mem_max_mb) {
            std::cout << "ResourceMonitor: Memory exceeds budget ("
                      << usage.mem_mb << "MB > " << config.resource.mem_max_mb << "MB)\n";
            exceeds = true;
        }
        
        if (usage.net_kbps > config.resource.net_max_kbps) {
            std::cout << "ResourceMonitor: Network exceeds budget ("
                      << usage.net_kbps << "KB/s > " << config.resource.net_max_kbps << "KB/s)\n";
            exceeds = true;
        }
        
        return exceeds;
    }
};

std::unique_ptr<ResourceMonitor> create_resource_monitor() {
    return std::make_unique<ResourceMonitorImpl>();
}

}
