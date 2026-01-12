#pragma once

#include <string>
#include <memory>

namespace agent {

// Forward declarations
struct Config;
struct Identity;

// AWS IoT Core communication credentials
struct CommunicationInfo {
    std::string region;
    std::string endpoint;
    std::string access_key;
    std::string secret_key;
    std::string token;
    
    bool is_valid() const {
        return !endpoint.empty() && !access_key.empty() && 
               !secret_key.empty() && !token.empty();
    }
};

// Manager for fetching AWS IoT communication credentials from backend
class CommInfoManager {
public:
    virtual ~CommInfoManager() = default;
    
    // Fetch communication credentials from backend
    // Returns true on success, false on failure
    virtual bool fetch_comm_info(const Identity& identity,
                                  const Config& config,
                                  CommunicationInfo& comm_info) = 0;
};

// Factory function to create implementation
std::unique_ptr<CommInfoManager> create_comm_info_manager();

}
