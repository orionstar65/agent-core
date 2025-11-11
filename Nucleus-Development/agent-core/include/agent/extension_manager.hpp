#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

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
};

class ExtensionManager {
public:
    virtual ~ExtensionManager() = default;
    
    //] Launch extensions from specs
    virtual void launch(const std::vector<ExtensionSpec>& specs) = 0;
    
    // Stop all running extensions
    virtual void stop_all() = 0;
    
    // Get status of all extensions
    virtual std::map<std::string, ExtState> status() const = 0;
};

// Create default implementation
std::unique_ptr<ExtensionManager> create_extension_manager();

}
