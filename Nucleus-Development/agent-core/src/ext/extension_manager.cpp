#include "agent/extension_manager.hpp"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace agent {

class ExtensionManagerImpl : public ExtensionManager {
public:
    void launch(const std::vector<ExtensionSpec>& specs) override {
        std::cout << "ExtensionManager: Launching " << specs.size() << " extensions\n";
        
        for (const auto& spec : specs) {
            std::cout << "  - Extension: " << spec.name
                      << ", Exec: " << spec.exec_path
                      << ", Critical: " << (spec.critical ? "yes" : "no") << "\n";
            
            // TODO: Actually launch process
            // for now just track state
            states_[spec.name] = ExtState::Running;
        }
    }
    
    void stop_all() override {
        std::cout << "ExtensionManager: Stopping all extensions\n";
        
        for (auto& [name, state] : states_) {
            std::cout << "  - Stopping: " << name << "\n";
            // TODO: send termination signal to process
            state = ExtState::Stopped;
        }
    }
    
    std::map<std::string, ExtState> status() const override {
        return states_;
    }

private:
    std::map<std::string, ExtState> states_;
};

std::unique_ptr<ExtensionManager> create_extension_manager() {
    return std::make_unique<ExtensionManagerImpl>();
}

}
