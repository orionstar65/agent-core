#include "agent/service_installer.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace agent {

class ServiceInstallerLinux : public ServiceInstaller {
public:
    ServiceInstallStatus check_status() override {
        // Check if systemd service file exists
        if (access("/etc/systemd/system/agent-core.service", F_OK) != 0) {
            return ServiceInstallStatus::NotInstalled;
        }
        
        // Check if service is running
        int ret = system("systemctl is-active --quiet agent-core");
        if (ret == 0) {
            return ServiceInstallStatus::Running;
        }
        
        return ServiceInstallStatus::Installed;
    }
    
    bool install(const std::string& binary_path, const std::string& config_path) override {
        std::cout << "ServiceInstaller: Installing systemd service...\n";
        
        // Check if running as root
        if (geteuid() != 0) {
            std::cerr << "ServiceInstaller: Must run as root to install service\n";
            return false;
        }
        
        // Create necessary directories
        if (mkdir("/var/lib/agent-core", 0755) != 0 && errno != EEXIST) {
            std::cerr << "ServiceInstaller: Failed to create /var/lib/agent-core\n";
            return false;
        }
        
        if (mkdir("/etc/agent-core", 0755) != 0 && errno != EEXIST) {
            std::cerr << "ServiceInstaller: Failed to create /etc/agent-core\n";
            return false;
        }
        
        // Copy binary to /usr/local/bin
        std::string install_binary = "/usr/local/bin/agent-core";
        std::string cp_cmd = "cp -f \"" + binary_path + "\" " + install_binary;
        if (system(cp_cmd.c_str()) != 0) {
            std::cerr << "ServiceInstaller: Failed to copy binary\n";
            return false;
        }
        
        system(("chmod 755 " + install_binary).c_str());
        
        // Copy config if provided and doesn't exist
        if (!config_path.empty() && access("/etc/agent-core/config.json", F_OK) != 0) {
            std::string cp_config = "cp \"" + config_path + "\" /etc/agent-core/config.json";
            system(cp_config.c_str());
        }
        
        // Create systemd service file
        std::ofstream service_file("/etc/systemd/system/agent-core.service");
        if (!service_file) {
            std::cerr << "ServiceInstaller: Failed to create service file\n";
            return false;
        }
        
        service_file << R"([Unit]
Description=Agent Core IoT Service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/local/bin/agent-core --config /etc/agent-core/config.json
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/agent-core

# Resource limits
CPUQuota=60%
MemoryMax=512M

[Install]
WantedBy=multi-user.target
)";
        
        service_file.close();
        
        // Reload systemd
        std::cout << "ServiceInstaller: Reloading systemd daemon...\n";
        if (system("systemctl daemon-reload") != 0) {
            std::cerr << "ServiceInstaller: Failed to reload systemd\n";
            return false;
        }
        
        // Enable service
        std::cout << "ServiceInstaller: Enabling service...\n";
        if (system("systemctl enable agent-core") != 0) {
            std::cerr << "ServiceInstaller: Failed to enable service\n";
            return false;
        }
        
        std::cout << "ServiceInstaller: Service installed successfully\n";
        return true;
    }
    
    bool start() override {
        std::cout << "ServiceInstaller: Starting service...\n";
        return system("systemctl start agent-core") == 0;
    }
    
    bool stop() override {
        std::cout << "ServiceInstaller: Stopping service...\n";
        return system("systemctl stop agent-core") == 0;
    }
};

std::unique_ptr<ServiceInstaller> create_service_installer() {
    return std::make_unique<ServiceInstallerLinux>();
}

}
