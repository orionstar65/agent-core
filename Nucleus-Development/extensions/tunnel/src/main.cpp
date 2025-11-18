#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

// Tunnel Extension - Manages VPN/IPsec tunnels
// Communicates with agent-core via ZeroMQ

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "Tunnel Extension: Received signal " << signum << "\n";
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Tunnel Extension v0.1.0 ===\n";
    std::cout << "Tunnel Extension: Starting\n";
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Parse arguments
    std::string config_path;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
            std::cout << "  Config: " << config_path << "\n";
        }
    }
    
    // TODO: Initialize ZeroMQ connection to agent-core
    std::cout << "Tunnel Extension: Connecting to agent-core via ZeroMQ...\n";
    std::cout << "  TODO: Implement ZeroMQ REQ/REP socket\n";
    std::cout << "  TODO: Subscribe to tunnel control messages\n";
    
    // Main loop - wait for commands from agent-core
    int heartbeat_count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        heartbeat_count++;
        std::cout << "Tunnel Extension: Heartbeat #" << heartbeat_count << "\n";
        
        // TODO: Process messages from agent-core
        // Expected messages:
        //   - StartTunnel (with mode: vpn|ipsec)
        //   - StopTunnel
        //   - GetTunnelStatus
        
        // TODO: Send TunnelReady event when tunnel established
        // TODO: Send TunnelFailed event on errors
    }
    
    std::cout << "Tunnel Extension: Shutting down\n";
    return 0;
}
