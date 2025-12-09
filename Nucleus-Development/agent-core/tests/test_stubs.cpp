#ifdef _WIN32
// Stub implementations for symbols referenced by service_host_win.cpp
// These are normally defined in main.cpp, but tests don't include main.cpp

#include "agent/service_host.hpp"
#include <string>
#include <stdexcept>

// Stub for g_config_path (used by ServiceMain)
std::string g_config_path;

// Stub for run_agent_core (used by ServiceMain)
// This should never be called in tests, but we provide a stub to satisfy the linker
void run_agent_core(agent::ServiceHost& service_host, const std::string& config_path) {
    // This is a stub - should never be called in test context
    // If it is called, it indicates a test setup issue
    throw std::runtime_error("run_agent_core should not be called in test context");
}

#endif // _WIN32

