#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

// Sample extension demonstrating the extension pattern
// In production, this would use ZeroMQ to communicate with agent-core

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "Sample Extension: Received signal " << signum << "\n";
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "Sample Extension: Starting\n";
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::cout << "  Arg[" << i << "]: " << argv[i] << "\n";
    }
    
    // TODO: Initialize ZeroMQ connection to agent-core
    std::cout << "Sample Extension: TODO - Connect to ZeroMQ bus\n";
    
    // Main loop
    int heartbeat_count = 0;
    while (g_running) {
        // Simulate work
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        heartbeat_count++;
        std::cout << "Sample Extension: Heartbeat #" << heartbeat_count << "\n";
        
        // TODO: Process messages from agent-core via ZeroMQ
    }
    
    std::cout << "Sample Extension: Shutting down\n";
    return 0;
}
