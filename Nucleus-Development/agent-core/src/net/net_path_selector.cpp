#include "agent/net_path_selector.hpp"
#include <iostream>

namespace agent {

class NetPathSelectorImpl : public NetPathSelector {
public:
    NetDecision decide(const Config& config, const Identity&) override {
        NetDecision decision;
        
        // Simple logic: if tunnel is enabled in config, request tunnel path
        if (config.tunnel.enabled) {
            decision.path = Path::Tunnel;
            decision.reason = "Tunnel enabled in configuration";
        } else {
            decision.path = Path::Direct;
            decision.reason = "Direct connection - tunnel not enabled";
        }
        
        std::cout << "Network path decision: " 
                  << (decision.path == Path::Tunnel ? "Tunnel" : "Direct")
                  << " (" << decision.reason << ")\n";
        
        return decision;
    }
};

std::unique_ptr<NetPathSelector> create_net_path_selector() {
    return std::make_unique<NetPathSelectorImpl>();
}

}
