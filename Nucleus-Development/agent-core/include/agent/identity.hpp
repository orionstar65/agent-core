#pragma once

#include <string>
#include "config.hpp"

namespace agent {

struct Identity {
    bool is_gateway{false};
    std::string device_serial;   // for devices
    std::string gateway_id;      // for gateways
    std::string uuid;            // unique identifier for authentication
};

Identity discover_identity(const Config& config);

} 
