#include "agent/identity.hpp"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fstream>
#endif

namespace agent {

Identity discover_identity(const Config& config) {
    Identity identity;
    
    // First check config overrides
    // For MVP serial number hardcoded to config
    // TODO: Modify this logic for final final product
    if (!config.identity.device_serial.empty() || !config.identity.gateway_id.empty()) {
        identity.is_gateway = config.identity.is_gateway;
        identity.device_serial = config.identity.device_serial;
        identity.gateway_id = config.identity.gateway_id;
        identity.uuid = config.identity.uuid;
        
        std::cout << "Identity from config: " 
                  << (identity.is_gateway ? "Gateway " + identity.gateway_id 
                                          : "Device " + identity.device_serial) 
                  << "\n";
        std::cout << "  UUID: " << identity.uuid << "\n";
        return identity;
    }
    
    // otherwise discover from system
    identity.is_gateway = false; // Default to device
    
#ifdef _WIN32
    // Windows: Try to get machine name as device serial
    char buffer[256];
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        identity.device_serial = std::string(buffer);
    } else {
        identity.device_serial = "WIN-UNKNOWN";
    }
#else
    // Linux: Try to get hostname
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        identity.device_serial = std::string(buffer);
    } else {
        identity.device_serial = "LINUX-UNKNOWN";
    }
    
    // Check for machine-id
    std::ifstream machine_id("/etc/machine-id");
    if (machine_id.is_open()) {
        std::string id;
        std::getline(machine_id, id);
        if (!id.empty()) {
            identity.device_serial = id;
        }
    }
#endif
    
    std::cout << "Discovered identity: Device " << identity.device_serial << "\n";
    return identity;
}

}
