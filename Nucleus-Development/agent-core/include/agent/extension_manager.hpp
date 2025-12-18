#pragma once

#include "agent/config.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

namespace agent {

enum class ExtState {
    Starting,
    Running,
    Crashed,
    Quarantined,
    Stopped
};

struct ExtensionSpec {
    std::string name;
    std::string exec_path;
    std::vector<std::string> args;
    bool critical{true};
    bool enabled{true};
};

struct ExtensionHealth {
    std::string name;
    ExtState state;
    int restart_count{0};
    std::chrono::steady_clock::time_point last_health_ping;
    std::chrono::steady_clock::time_point last_restart_time;
    std::chrono::steady_clock::time_point crash_time;
    std::chrono::steady_clock::time_point quarantine_start_time;
    bool responding{false};
};

class ExtensionManager {
public:
    virtual ~ExtensionManager() = default;
    
    /// Launch extensions from specs
    virtual void launch(const std::vector<ExtensionSpec>& specs) = 0;
    
    /// Stop all running extensions
    virtual void stop_all() = 0;
    
    /// Stop a specific extension
    virtual void stop(const std::string& name) = 0;
    
    /// Monitor extensions (check for crashes, handle restarts)
    virtual void monitor() = 0;
    
    /// Send health ping to all extensions
    virtual void health_ping() = 0;
    
    /// Get status of all extensions
    virtual std::map<std::string, ExtState> status() const = 0;
    
    /// Get detailed health info for all extensions
    virtual std::map<std::string, ExtensionHealth> health_status() const = 0;
};

// Create extension manager with configuration
std::unique_ptr<ExtensionManager> create_extension_manager(const Config::Extensions& config);

// Load extension specs from manifest file
std::vector<ExtensionSpec> load_extension_manifest(const std::string& manifest_path);

}
