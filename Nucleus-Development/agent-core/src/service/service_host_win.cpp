#ifdef _WIN32

#include "agent/service_host.hpp"
#include <windows.h>
#include <iostream>
#include <atomic>

namespace agent {

static std::atomic<bool> g_should_stop{false};
static SERVICE_STATUS g_service_status = {0};
static SERVICE_STATUS_HANDLE g_service_status_handle = nullptr;
static std::function<void()> g_main_loop_func;

void WINAPI ServiceCtrlHandler(DWORD ctrl_code);
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
void ReportServiceStatus(DWORD current_state, DWORD exit_code, DWORD wait_hint);

class ServiceHostWin : public ServiceHost {
public:
    ServiceHostWin() = default;
    
    bool initialize() override {
        std::cout << "ServiceHostWin: Initializing Windows Service\n";
        return true;
    }
    
    void run(std::function<void()> main_loop) override {
        g_main_loop_func = main_loop;
        
        SERVICE_TABLE_ENTRY service_table[] = {
            {const_cast<LPSTR>("AgentCore"), ServiceMain},
            {nullptr, nullptr}
        };
        
        if (!StartServiceCtrlDispatcher(service_table)) {
            DWORD error = GetLastError();
            std::cerr << "ServiceHostWin: StartServiceCtrlDispatcher failed (error: "
                      << error << ")\n";
        }
    }
    
    bool should_stop() const override {
        return g_should_stop;
    }
    
    void shutdown() override {
        std::cout << "ServiceHostWin: Shutting down\n";
        g_should_stop = true;
        
        if (g_service_status_handle) {
            ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
        }
    }
};

void WINAPI ServiceCtrlHandler(DWORD ctrl_code) {
    switch (ctrl_code) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            std::cout << "ServiceHostWin: Stop/Shutdown requested\n";
            ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            g_should_stop = true;
            break;
            
        case SERVICE_CONTROL_INTERROGATE:
            ReportServiceStatus(g_service_status.dwCurrentState, NO_ERROR, 0);
            break;
            
        default:
            break;
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_service_status_handle = RegisterServiceCtrlHandler(
        "AgentCore",
        ServiceCtrlHandler
    );
    
    if (!g_service_status_handle) {
        return;
    }
    
    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwServiceSpecificExitCode = 0;
    
    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    
    if (g_main_loop_func) {
        g_main_loop_func();
    }
    
    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

void ReportServiceStatus(DWORD current_state, DWORD exit_code, DWORD wait_hint) {
    static DWORD checkpoint = 1;
    
    g_service_status.dwCurrentState = current_state;
    g_service_status.dwWin32ExitCode = exit_code;
    g_service_status.dwWaitHint = wait_hint;
    
    if (current_state == SERVICE_START_PENDING) {
        g_service_status.dwControlsAccepted = 0;
    } else {
        g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }
    
    if (current_state == SERVICE_RUNNING || current_state == SERVICE_STOPPED) {
        g_service_status.dwCheckPoint = 0;
    } else {
        g_service_status.dwCheckPoint = checkpoint++;
    }
    
    SetServiceStatus(g_service_status_handle, &g_service_status);
}

std::unique_ptr<ServiceHost> create_service_host() {
    return std::make_unique<ServiceHostWin>();
}

}

#endif // _WIN32
