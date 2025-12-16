#pragma once

#include <string>
#include "config.hpp"

namespace agent {

struct TunnelInfo {
    bool enabled{false};
};

struct Identity {
    bool is_gateway{false};
    std::string device_serial;   // for devices (legacy, mapped from serial_number)
    std::string gateway_id;      // for gateways
    std::string uuid;            // unique identifier for authentication
    
    // New identity fields
    std::string serial_number;      // device serial number
    std::string material_number;    // optional material number
    std::string product_name;       // optional product name
    std::string software_version;   // optional software version
    TunnelInfo tunnel_info;          // tunnel information
};

Identity discover_identity(const Config& config);

} 
