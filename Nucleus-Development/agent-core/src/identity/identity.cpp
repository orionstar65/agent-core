#include "agent/identity.hpp"
#include "agent/uuid.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace agent {

#ifdef _WIN32
// Read identity information from Windows Registry
// Registry path: HKLM\SOFTWARE\AgentCore\Identity
// Returns true if any values were successfully read
static bool read_identity_from_registry(Identity& identity) {
    const char* reg_path = "SOFTWARE\\AgentCore\\Identity";
    HKEY hkey = nullptr;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, reg_path, 0, KEY_READ, &hkey);
    
    if (result != ERROR_SUCCESS) {
        return false;
    }
    
    bool found_any = false;
    char buffer[512];
    DWORD buffer_size = sizeof(buffer);
    DWORD value_type = 0;
    
    // Read serialNumber
    buffer_size = sizeof(buffer);
    result = RegQueryValueExA(hkey, "serialNumber", nullptr, &value_type, 
                               reinterpret_cast<LPBYTE>(buffer), &buffer_size);
    if (result == ERROR_SUCCESS && value_type == REG_SZ && buffer_size > 0) {
        identity.serial_number = std::string(buffer, buffer_size - 1); // Remove null terminator
        found_any = true;
    }
    
    // Read materialNumber
    buffer_size = sizeof(buffer);
    result = RegQueryValueExA(hkey, "materialNumber", nullptr, &value_type,
                               reinterpret_cast<LPBYTE>(buffer), &buffer_size);
    if (result == ERROR_SUCCESS && value_type == REG_SZ && buffer_size > 0) {
        identity.material_number = std::string(buffer, buffer_size - 1);
        found_any = true;
    }
    
    // Read productName
    buffer_size = sizeof(buffer);
    result = RegQueryValueExA(hkey, "productName", nullptr, &value_type,
                               reinterpret_cast<LPBYTE>(buffer), &buffer_size);
    if (result == ERROR_SUCCESS && value_type == REG_SZ && buffer_size > 0) {
        identity.product_name = std::string(buffer, buffer_size - 1);
        found_any = true;
    }
    
    // Read softwareVersion
    buffer_size = sizeof(buffer);
    result = RegQueryValueExA(hkey, "softwareVersion", nullptr, &value_type,
                               reinterpret_cast<LPBYTE>(buffer), &buffer_size);
    if (result == ERROR_SUCCESS && value_type == REG_SZ && buffer_size > 0) {
        identity.software_version = std::string(buffer, buffer_size - 1);
        found_any = true;
    }
    
    RegCloseKey(hkey);
    return found_any;
}
#endif

// Read identity information from identity.json file
// directory_path: Directory to look for identity.json
// Returns true if file was successfully read and parsed
static bool read_identity_from_json(const std::string& directory_path, Identity& identity) {
    std::filesystem::path identity_json_path = std::filesystem::path(directory_path) / "identity.json";
    
    std::ifstream file(identity_json_path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        json j = json::parse(file);
        
        // Parse serialNumber
        if (j.contains("serialNumber") && j["serialNumber"].is_string()) {
            identity.serial_number = j["serialNumber"].get<std::string>();
        }
        
        // Parse materialNumber
        if (j.contains("materialNumber") && j["materialNumber"].is_string()) {
            identity.material_number = j["materialNumber"].get<std::string>();
        }
        
        // Parse productName
        if (j.contains("productName") && j["productName"].is_string()) {
            identity.product_name = j["productName"].get<std::string>();
        }
        
        // Parse softwareVersion
        if (j.contains("softwareVersion") && j["softwareVersion"].is_string()) {
            identity.software_version = j["softwareVersion"].get<std::string>();
        }
        
        // Parse tunnelInfo
        if (j.contains("tunnelInfo") && j["tunnelInfo"].is_object()) {
            auto& tunnel_info = j["tunnelInfo"];
            if (tunnel_info.contains("enabled") && tunnel_info["enabled"].is_boolean()) {
                identity.tunnel_info.enabled = tunnel_info["enabled"].get<bool>();
            }
        }
        
        // Parse isGateway
        if (j.contains("isGateway") && j["isGateway"].is_boolean()) {
            identity.is_gateway = j["isGateway"].get<bool>();
        }
        
        return true;
    } catch (const json::exception& e) {
        std::cerr << "Error parsing identity.json: " << e.what() << "\n";
        return false;
    }
}

Identity discover_identity(const Config& config) {
    Identity identity;
    
    // Priority 1: Check config overrides (highest priority)
    if (!config.identity.device_serial.empty() || !config.identity.gateway_id.empty()) {
        identity.is_gateway = config.identity.is_gateway;
        identity.device_serial = config.identity.device_serial;
        identity.gateway_id = config.identity.gateway_id;
        identity.uuid = config.identity.uuid;
        
        // Map device_serial to serial_number for backward compatibility
        if (!identity.device_serial.empty()) {
            identity.serial_number = identity.device_serial;
        }
        
        // Copy tunnel info from config
        identity.tunnel_info.enabled = config.tunnel.enabled;
        
        std::cout << "Identity from config: " 
                  << (identity.is_gateway ? "Gateway " + identity.gateway_id 
                                          : "Device " + identity.device_serial) 
                  << "\n";
        std::cout << "  UUID: " << identity.uuid << "\n";
        return identity;
    }
    
    // Priority 2: Try Windows Registry (Windows only)
#ifdef _WIN32
    if (read_identity_from_registry(identity)) {
        std::cout << "Identity from Windows Registry:\n";
        if (!identity.serial_number.empty()) {
            std::cout << "  Serial Number: " << identity.serial_number << "\n";
            identity.device_serial = identity.serial_number; // Map for backward compatibility
        }
        if (!identity.material_number.empty()) {
            std::cout << "  Material Number: " << identity.material_number << "\n";
        }
        if (!identity.product_name.empty()) {
            std::cout << "  Product Name: " << identity.product_name << "\n";
        }
        if (!identity.software_version.empty()) {
            std::cout << "  Software Version: " << identity.software_version << "\n";
        }
        
        // Copy tunnel info from config (registry doesn't store tunnel info)
        identity.tunnel_info.enabled = config.tunnel.enabled;
    }
#endif
    
    // Priority 3: Fallback to identity.json (if registry didn't provide data or not Windows)
    bool has_identity_data = !identity.serial_number.empty() || 
                             !identity.material_number.empty() || 
                             !identity.product_name.empty() || 
                             !identity.software_version.empty();
    
    // Try to read identity.json from executable directory (common location)
    // Also try current working directory as fallback
    std::vector<std::string> search_dirs;
    
    // Get executable directory
#ifdef _WIN32
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) != 0) {
        std::filesystem::path exe_file(exe_path);
        std::string exe_dir = exe_file.parent_path().string();
        if (!exe_dir.empty()) {
            search_dirs.push_back(exe_dir);
        }
    }
#else
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::filesystem::path exe_file(exe_path);
        std::string exe_dir = exe_file.parent_path().string();
        if (!exe_dir.empty()) {
            search_dirs.push_back(exe_dir);
        }
    }
#endif
    
    // Add current working directory
    try {
        search_dirs.push_back(std::filesystem::current_path().string());
    } catch (...) {
        // Ignore if we can't get current path
    }
    
    bool json_read = false;
    for (const auto& dir : search_dirs) {
        if (!has_identity_data) {
            // No registry data - read all fields from JSON
            if (read_identity_from_json(dir, identity)) {
                json_read = true;
                std::cout << "Identity from identity.json (in " << dir << "):\n";
                if (!identity.serial_number.empty()) {
                    std::cout << "  Serial Number: " << identity.serial_number << "\n";
                    identity.device_serial = identity.serial_number;
                }
                if (!identity.material_number.empty()) {
                    std::cout << "  Material Number: " << identity.material_number << "\n";
                }
                if (!identity.product_name.empty()) {
                    std::cout << "  Product Name: " << identity.product_name << "\n";
                }
                if (!identity.software_version.empty()) {
                    std::cout << "  Software Version: " << identity.software_version << "\n";
                }
                if (identity.tunnel_info.enabled) {
                    std::cout << "  Tunnel Enabled: true\n";
                }
                has_identity_data = true;
                break; // Found and read, no need to try other directories
            }
        } else {
            // Registry provided data - read JSON separately just for tunnel info and isGateway
            Identity json_identity;
            if (read_identity_from_json(dir, json_identity)) {
                json_read = true;
                identity.tunnel_info = json_identity.tunnel_info;
                if (json_identity.is_gateway) {
                    identity.is_gateway = json_identity.is_gateway;
                }
                break; // Found and read, no need to try other directories
            }
        }
    }
    
    // Priority 4: System discovery (fallback if no registry/JSON data)
    if (!has_identity_data) {
        identity.is_gateway = false; // Default to device
        
#ifdef _WIN32
        // Windows: Try to get machine name as device serial
        char buffer[256];
        DWORD size = sizeof(buffer);
        if (GetComputerNameA(buffer, &size)) {
            identity.serial_number = std::string(buffer);
            identity.device_serial = identity.serial_number;
        } else {
            identity.serial_number = "WIN-UNKNOWN";
            identity.device_serial = identity.serial_number;
        }
#else
        // Linux: Try to get hostname
        char buffer[256];
        if (gethostname(buffer, sizeof(buffer)) == 0) {
            identity.serial_number = std::string(buffer);
            identity.device_serial = identity.serial_number;
        } else {
            identity.serial_number = "LINUX-UNKNOWN";
            identity.device_serial = identity.serial_number;
        }
        
        // Check for machine-id
        std::ifstream machine_id("/etc/machine-id");
        if (machine_id.is_open()) {
            std::string id;
            std::getline(machine_id, id);
            if (!id.empty()) {
                identity.serial_number = id;
                identity.device_serial = identity.serial_number;
            }
        }
#endif
        std::cout << "Discovered identity from system: Device " << identity.device_serial << "\n";
    }
    
    // Gateway mode handling: If isGateway=true and no standard identity fields, use UUID as unique identifier
    if (identity.is_gateway) {
        bool has_standard_fields = !identity.serial_number.empty() || 
                                   !identity.material_number.empty() || 
                                   !identity.product_name.empty() || 
                                   !identity.software_version.empty();
        
        if (!has_standard_fields) {
            std::cerr << "Warning: Gateway mode enabled but no standard identity fields found. "
                      << "Using UUID as unique identifier.\n";
            
            // Generate UUID if not already set
            if (identity.uuid.empty()) {
                identity.uuid = util::generate_uuid();
            }
            
            // Set gateway_id from UUID if not already set
            if (identity.gateway_id.empty()) {
                identity.gateway_id = identity.uuid;
            }
            
            std::cout << "  Gateway ID (from UUID): " << identity.gateway_id << "\n";
            std::cout << "  UUID: " << identity.uuid << "\n";
        } else {
            // Standard fields present, use serial_number as gateway_id if not set
            if (identity.gateway_id.empty() && !identity.serial_number.empty()) {
                identity.gateway_id = identity.serial_number;
            }
        }
    } else {
        // Device mode: map serial_number to device_serial if not already set
        if (identity.device_serial.empty() && !identity.serial_number.empty()) {
            identity.device_serial = identity.serial_number;
        }
    }
    
    // Ensure UUID is set (generate if missing)
    if (identity.uuid.empty()) {
        identity.uuid = util::generate_uuid();
    }
    
    return identity;
}

}
