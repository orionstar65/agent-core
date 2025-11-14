#ifndef _WIN32

#include "agent/service_host.hpp"
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <atomic>

namespace agent {

static std::atomic<bool> g_should_stop{false};

static void signal_handler(int signum) {
    std::cout << "Received signal " << signum << ", initiating shutdown\n";
    g_should_stop = true;
}

class ServiceHostLinux : public ServiceHost {
public:
    ServiceHostLinux() {}
    
    bool initialize() override {
        std::cout << "ServiceHostLinux: Initializing Linux daemon\n";
        
        // Setup signal handlers
        struct sigaction sa;
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        
        return true;
    }
    
    void run(std::function<void()> main_loop) override {
        std::cout << "ServiceHostLinux: Starting main loop\n";
        
        // Run the main loop
        main_loop();
    }
    
    bool should_stop() const override {
        return g_should_stop;
    }
    
    void shutdown() override {
        std::cout << "ServiceHostLinux: Shutting down\n";
        g_should_stop = true;
    }
};

std::unique_ptr<ServiceHost> create_service_host() {
    return std::make_unique<ServiceHostLinux>();
}

}

#endif // !_WIN32
