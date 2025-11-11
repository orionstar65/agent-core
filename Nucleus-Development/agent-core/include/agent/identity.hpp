#pragma once

#include <string>
#include "config.hpp"

namespace agent {

struct Identity {
    bool is_gateway{false};
    std::string device_serial;   // for devices
    std::string gateway_id;      // for gateways
};

Identity discover_identity(const Config& config);

} 
