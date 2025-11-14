#ifdef _WIN32

#include "agent/service_host.hpp"
#include <windows.h>
#include <iostream>
#include <atomic>

namespace agent {

class ServiceHostWin : public ServiceHost {
public:
    ServiceHostWin() : should_stop_(false) {}
    
    bool initialize() override {
        std::cout << "ServiceHostWin: Initializing Windows service\n";
        // TODO: Implement proper Windows Service initialization
        // For now just run as console app
        return true;
    }
    
    void run(std::function<void()> main_loop) override {
        std::cout << "ServiceHostWin: Starting main loop\n";
        
        // TODO: Implement proper service loop with SCM integration
        // For now just run the main loop
        main_loop();
    }
    
    bool should_stop() const override {
        return should_stop_;
    }
    
    void shutdown() override {
        std::cout << "ServiceHostWin: Shutting down\n";
        should_stop_ = true;
    }

private:
    std::atomic<bool> should_stop_;
};

std::unique_ptr<ServiceHost> create_service_host() {
    return std::make_unique<ServiceHostWin>();
}

}

#endif // _WIN32
