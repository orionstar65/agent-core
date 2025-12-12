#pragma once

#include <string>
#include <memory>

namespace agent {

enum class ServiceInstallStatus {
    NotInstalled,
    Installed,
    Running,
    Failed
};

class ServiceInstaller {
public:
    virtual ~ServiceInstaller() = default;
    
    /// Check if service is installed
    virtual ServiceInstallStatus check_status() = 0;
    
    /// Install service
    virtual bool install(const std::string& binary_path, const std::string& config_path) = 0;
    
    /// Start service
    virtual bool start() = 0;
    
    /// Stop service
    virtual bool stop() = 0;
};

std::unique_ptr<ServiceInstaller> create_service_installer();

}
