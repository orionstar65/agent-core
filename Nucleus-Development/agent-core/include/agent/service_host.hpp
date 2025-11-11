#pragma once

#include <memory>
#include <functional>

namespace agent {

class ServiceHost {
public:
    virtual ~ServiceHost() = default;
    
    // Initialize service/daemon
    virtual bool initialize() = 0;
    
    // Run main service loop
    // Returns when service should stop (via signal or service control)
    virtual void run(std::function<void()> main_loop) = 0;
    
    // Check if shutdown requested
    virtual bool should_stop() const = 0;
    
    // Shutdown service
    virtual void shutdown() = 0;
};

// Create platform-specific service host
std::unique_ptr<ServiceHost> create_service_host();

}