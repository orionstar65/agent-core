#pragma once

#include <string>
#include <memory>
#include "config.hpp"
#include "identity.hpp"

namespace agent {

enum class Path {
    Direct,
    Tunnel
};

struct NetDecision {
    Path path{Path::Direct};
    std::string reason;
};

class NetPathSelector {
public:
    virtual ~NetPathSelector() = default;
    
    // Decide whether to use direct or tunnel path
    virtual NetDecision decide(const Config& config, const Identity& identity) = 0;
};

std::unique_ptr<NetPathSelector> create_net_path_selector();

}
