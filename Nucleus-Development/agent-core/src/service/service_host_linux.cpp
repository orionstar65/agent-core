#ifndef _WIN32

#include "agent/service_host.hpp"
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>
#include <atomic>

namespace agent {

static std::atomic<bool> g_should_stop{false};
static std::atomic<bool> g_reload_config{false};

static void signal_handler(int signum) {
    switch (signum) {
        case SIGTERM:
        case SIGINT:
            std::cout << "ServiceHostLinux: Received signal " << signum 
                      << " (SIGTERM/SIGINT), initiating graceful shutdown\n";
            g_should_stop = true;
            break;
            
        case SIGHUP:
            std::cout << "ServiceHostLinux: Received SIGHUP, reload config requested\n";
            g_reload_config = true;
            break;
            
        default:
            std::cout << "ServiceHostLinux: Received unhandled signal " << signum << "\n";
            break;
    }
}

class ServiceHostLinux : public ServiceHost {
public:
    ServiceHostLinux() = default;
    
    bool initialize() override {
        std::cout << "ServiceHostLinux: Initializing Linux daemon\n";
        
        struct sigaction sa;
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        
        if (sigaction(SIGTERM, &sa, nullptr) < 0) {
            std::cerr << "ServiceHostLinux: Failed to setup SIGTERM handler\n";
            return false;
        }
        
        if (sigaction(SIGINT, &sa, nullptr) < 0) {
            std::cerr << "ServiceHostLinux: Failed to setup SIGINT handler\n";
            return false;
        }
        
        if (sigaction(SIGHUP, &sa, nullptr) < 0) {
            std::cerr << "ServiceHostLinux: Failed to setup SIGHUP handler\n";
            return false;
        }
        
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, nullptr);
        
        std::cout << "ServiceHostLinux: Signal handlers registered\n";
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
        std::cout << "ServiceHostLinux: Initiating shutdown\n";
        g_should_stop = true;
    }
};

std::unique_ptr<ServiceHost> create_service_host() {
    return std::make_unique<ServiceHostLinux>();
}

}

#endif // !_WIN32
