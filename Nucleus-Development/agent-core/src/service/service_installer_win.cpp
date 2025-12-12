#ifdef _WIN32

#include "agent/service_installer.hpp"
#include <windows.h>
#include <iostream>

namespace agent {

class ServiceInstallerWin : public ServiceInstaller {
public:
    ServiceInstallStatus check_status() override {
        SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) {
            return ServiceInstallStatus::Failed;
        }
        
        SC_HANDLE service = OpenService(scm, "AgentCore", SERVICE_QUERY_STATUS);
        if (!service) {
            CloseServiceHandle(scm);
            return ServiceInstallStatus::NotInstalled;
        }
        
        SERVICE_STATUS status;
        if (QueryServiceStatus(service, &status)) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            
            if (status.dwCurrentState == SERVICE_RUNNING) {
                return ServiceInstallStatus::Running;
            }
            return ServiceInstallStatus::Installed;
        }
        
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return ServiceInstallStatus::Failed;
    }
    
    bool install(const std::string& binary_path, const std::string& config_path) override {
        std::cout << "ServiceInstaller: Installing Windows service...\n";
        
        SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
        if (!scm) {
            std::cerr << "ServiceInstaller: Failed to open Service Control Manager\n";
            return false;
        }
        
        std::string command_line = "\"" + binary_path + "\" --config \"" + config_path + "\"";
        
        SC_HANDLE service = CreateService(
            scm,
            "AgentCore",
            "Agent Core IoT Service",
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            command_line.c_str(),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        );
        
        if (!service) {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_EXISTS) {
                std::cout << "ServiceInstaller: Service already exists\n";
                CloseServiceHandle(scm);
                return true;
            }
            std::cerr << "ServiceInstaller: Failed to create service (error: " << error << ")\n";
            CloseServiceHandle(scm);
            return false;
        }
        
        std::cout << "ServiceInstaller: Service installed successfully\n";
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return true;
    }
    
    bool start() override {
        std::cout << "ServiceInstaller: Starting service...\n";
        
        SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) {
            return false;
        }
        
        SC_HANDLE service = OpenService(scm, "AgentCore", SERVICE_START);
        if (!service) {
            CloseServiceHandle(scm);
            return false;
        }
        
        bool result = StartService(service, 0, nullptr) != 0;
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return result;
    }
    
    bool stop() override {
        std::cout << "ServiceInstaller: Stopping service...\n";
        
        SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) {
            return false;
        }
        
        SC_HANDLE service = OpenService(scm, "AgentCore", SERVICE_STOP);
        if (!service) {
            CloseServiceHandle(scm);
            return false;
        }
        
        SERVICE_STATUS status;
        bool result = ControlService(service, SERVICE_CONTROL_STOP, &status) != 0;
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return result;
    }
};

std::unique_ptr<ServiceInstaller> create_service_installer() {
    return std::make_unique<ServiceInstallerWin>();
}

}

#endif // _WIN32
