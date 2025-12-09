#ifdef _WIN32

#include "agent/service_host.hpp"
#include "agent/path_utils.hpp"
#include <windows.h>
#include <iostream>
#include <atomic>
#include <string>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <io.h>
#include <fcntl.h>

// Forward declarations for agent core setup (in global namespace, defined in main.cpp)
extern std::string g_config_path;
extern void run_agent_core(agent::ServiceHost& service_host, const std::string& config_path);

namespace agent {

// Service name for SCM
#define SERVICE_NAME "AgentCore"

// Forward declaration
class ServiceHostWin;

// Static instances for service callbacks
static ServiceHostWin* g_service_host_instance = nullptr;
static SERVICE_STATUS_HANDLE g_service_status_handle = nullptr;
static SERVICE_STATUS g_service_status = {
    SERVICE_WIN32_OWN_PROCESS,  // dwServiceType
    0,                          // dwCurrentState
    0,                          // dwControlsAccepted
    NO_ERROR,                   // dwWin32ExitCode
    0,                          // dwServiceSpecificExitCode
    0,                          // dwCheckPoint
    0                           // dwWaitHint
};

// Forward declarations
static VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
static DWORD WINAPI ServiceCtrlHandler(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext);
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);

class ServiceHostWin : public ServiceHost {
public:
    ServiceHostWin() : should_stop_(false), is_service_(false), main_loop_(nullptr) {
        // Set static instance for console handler
        g_service_host_instance = this;
    }
    
    ~ServiceHostWin() {
        if (g_service_host_instance == this) {
            g_service_host_instance = nullptr;
        }
        if (g_service_status_handle && is_service_) {
            report_status(SERVICE_STOPPED, NO_ERROR, 0);
        }
    }
    
    bool initialize() override {
        // Install console control handler for Ctrl+C and Ctrl+Break (for console mode)
        if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
            std::cerr << "ServiceHostWin: Warning - Failed to install console control handler\n";
            // Continue anyway - shutdown() can still be called directly
        }
        
        return true;
    }
    
    void run(std::function<void()> main_loop) override {
        main_loop_ = main_loop;
        
        if (is_service_) {
            // Running as service - ServiceMain will call the main loop
            // This should not be reached in normal service flow
        } else {
            // Running as console app
            main_loop();
        }
    }
    
    bool should_stop() const override {
        return should_stop_.load();
    }
    
    void shutdown() override {
        should_stop_.store(true);
        
        if (is_service_ && g_service_status_handle) {
            report_status(SERVICE_STOP_PENDING, NO_ERROR, 0);
        }
    }
    
    // Called by ServiceMain to set service mode
    void set_service_mode() {
        is_service_ = true;
    }
    
    // Called by ServiceMain to run the service
    // Note: run() already sets up main_loop_, so we just need to call it
    void run_service() {
        // main_loop_ is set in run(), just execute it
        if (main_loop_) {
            main_loop_();
        }
    }
    
    // Report service status to SCM
    void report_status(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
        if (!g_service_status_handle) {
            return;
        }
        
        static DWORD dwCheckPoint = 1;
        
        g_service_status.dwCurrentState = dwCurrentState;
        g_service_status.dwWin32ExitCode = dwWin32ExitCode;
        g_service_status.dwWaitHint = dwWaitHint;
        
        if (dwCurrentState == SERVICE_START_PENDING) {
            g_service_status.dwControlsAccepted = 0;
        } else {
            g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        }
        
        if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) {
            g_service_status.dwCheckPoint = 0;
        } else {
            g_service_status.dwCheckPoint = dwCheckPoint++;
        }
        
        SetServiceStatus(g_service_status_handle, &g_service_status);
    }

private:
    std::atomic<bool> should_stop_;
    bool is_service_;
    std::function<void()> main_loop_;
};

// Console control handler (must be after ServiceHostWin definition)
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (g_service_host_instance) {
        switch (dwCtrlType) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
                std::cout << "ServiceHostWin: Received shutdown signal (type: " << dwCtrlType << ")\n";
                g_service_host_instance->shutdown();
                return TRUE;
            default:
                return FALSE;
        }
    }
    return FALSE;
}

// Static service host instance for service mode
static std::unique_ptr<agent::ServiceHost> g_service_host_static = nullptr;

// Helper function to write to Windows Event Log (works even if file logging fails)
static void WriteEventLog(const std::string& message, WORD eventType = EVENTLOG_INFORMATION_TYPE) {
    HANDLE hEventLog = RegisterEventSourceA(nullptr, "AgentCore");
    if (hEventLog) {
        const char* msg = message.c_str();
        ReportEventA(hEventLog, eventType, 0, 0, nullptr, 1, 0, &msg, nullptr);
        DeregisterEventSource(hEventLog);
    }
}

// Service entry point called by SCM
static VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    // CRITICAL: Write to a simple log file IMMEDIATELY to confirm ServiceMain is called
    // This works even if Event Log source isn't registered
    try {
        std::string exe_dir = util::get_executable_directory();
        if (!exe_dir.empty()) {
            std::filesystem::path debug_log = std::filesystem::path(exe_dir) / "service-debug.log";
            std::ofstream debug_file(debug_log.string(), std::ios::app);
            if (debug_file.is_open()) {
                debug_file << "[" << __TIME__ << "] ServiceMain: Entry point called\n";
                debug_file.flush();
                debug_file.close();
            }
        }
    } catch (...) {
        // Ignore - can't log if this fails
    }
    
    // CRITICAL: Initialize service status structure FIRST (before any other operations)
    // This must be done before RegisterServiceCtrlHandlerEx to avoid ERROR_INVALID_PARAMETER
    ZeroMemory(&g_service_status, sizeof(SERVICE_STATUS));
    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwServiceSpecificExitCode = 0;
    g_service_status.dwCurrentState = SERVICE_START_PENDING;
    g_service_status.dwControlsAccepted = 0;  // No controls accepted during START_PENDING
    g_service_status.dwWin32ExitCode = NO_ERROR;
    g_service_status.dwCheckPoint = 0;
    g_service_status.dwWaitHint = 30000;  // 30 seconds
    
    // Register service control handler IMMEDIATELY (must be first operation after status init)
    g_service_status_handle = RegisterServiceCtrlHandlerEx(
        SERVICE_NAME,
        ServiceCtrlHandler,
        nullptr
    );
    
    if (!g_service_status_handle) {
        DWORD error = GetLastError();
        char errorMsg[256];
        sprintf_s(errorMsg, sizeof(errorMsg), "ServiceMain: Failed to register control handler, error: %lu", error);
        WriteEventLog(errorMsg, EVENTLOG_ERROR_TYPE);
        // Can't report status without handle - service will fail with error 87
        return;
    }
    
    // CRITICAL: Report START_PENDING IMMEDIATELY after getting handle
    // This must happen within 30 seconds or SCM will timeout with error 87
    if (!SetServiceStatus(g_service_status_handle, &g_service_status)) {
        DWORD error = GetLastError();
        char errorMsg[256];
        sprintf_s(errorMsg, sizeof(errorMsg), "ServiceMain: Failed to report START_PENDING, error: %lu", error);
        WriteEventLog(errorMsg, EVENTLOG_ERROR_TYPE);
        return;
    }
    
    // Update checkpoint to show progress
    g_service_status.dwCheckPoint = 1;
    g_service_status.dwWaitHint = 30000;  // Give ourselves 30 seconds
    if (!SetServiceStatus(g_service_status_handle, &g_service_status)) {
        WriteEventLog("ServiceMain: Failed to update checkpoint", EVENTLOG_ERROR_TYPE);
        return;
    }
    
    // Wrap entire service initialization in try-catch to ensure we always report status
    try {
    
    // Set up log file redirection BEFORE any std::cout/std::cerr calls
    // Read log path from environment variable (set by install script)
    char* env_log = nullptr;
    size_t env_log_len = 0;
    std::string log_path;
    if (_dupenv_s(&env_log, &env_log_len, "AGENT_CORE_LOG_PATH") == 0 && env_log != nullptr) {
        log_path = std::string(env_log);
        free(env_log);
    }
    
    // Fallback: Use default log path if environment variable not set
    // This ensures we always have logging, even if registry env vars aren't read correctly
    if (log_path.empty()) {
        // Try to get executable directory for default log path
        std::string exe_dir = util::get_executable_directory();
        if (!exe_dir.empty()) {
            std::filesystem::path default_log = std::filesystem::path(exe_dir) / "agent-core-service.log";
            log_path = default_log.string();
        }
    }
    
    // Redirect stdout/stderr to log file if specified
    // This is critical for Windows services where stdout/stderr don't go anywhere by default
    if (!log_path.empty()) {
        try {
            // Create log directory if it doesn't exist
            std::filesystem::path log_file_path(log_path);
            std::filesystem::path log_dir = log_file_path.parent_path();
            if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
                std::filesystem::create_directories(log_dir);
            }
            
            // Redirect stdout and stderr to log file using freopen
            // This ensures all std::cout and std::cerr output goes to the log file
            FILE* log_file = nullptr;
            // Redirect stdout to log file
            if (freopen_s(&log_file, log_path.c_str(), "a", stdout) == 0) {
                // Redirect stderr to the same file
                freopen_s(&log_file, log_path.c_str(), "a", stderr);
                
                // Write initial marker
                std::cout << "\n=== Service Starting ===\n";
                std::cout.flush();
            }
        } catch (...) {
            // If log file setup fails, continue anyway (service can still run)
        }
    }
    
    // Now we can safely read config and initialize
    // Read config path from environment variable (set by install script)
    // Windows services read environment variables from the registry
    std::string config_path;
    
    // First, try to read from environment variable (set by install script)
    char* env_config = nullptr;
    size_t env_config_len = 0;
    if (_dupenv_s(&env_config, &env_config_len, "AGENT_CORE_CONFIG_PATH") == 0 && env_config != nullptr) {
        config_path = std::string(env_config);
        free(env_config);
    }
    
    // Fallback to g_config_path (set from main() if running as console first)
    if (config_path.empty()) {
        config_path = ::g_config_path;
    }
    
    // Fallback to parsing service arguments
    if (config_path.empty() && argc > 0) {
        for (DWORD i = 0; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--config" && i + 1 < argc) {
                config_path = argv[++i];
                break;
            }
        }
    }
    
    // Final fallback to default
    if (config_path.empty()) {
        config_path = "config/dev.json";
    }
    
    // Create service host instance
    g_service_host_static = create_service_host();
    g_service_host_instance = static_cast<ServiceHostWin*>(g_service_host_static.get());
    
    if (!g_service_host_instance) {
        std::cerr << "ServiceHostWin: Failed to create service host instance\n";
        std::cerr.flush();
        g_service_status.dwCurrentState = SERVICE_STOPPED;
        g_service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_service_status.dwCheckPoint = 0;
        SetServiceStatus(g_service_status_handle, &g_service_status);
        return;
    }
    
    g_service_host_instance->set_service_mode();
    
    // Report starting status
    g_service_host_instance->report_status(SERVICE_START_PENDING, NO_ERROR, 10000);
    
    // Initialize the service
    if (!g_service_host_instance->initialize()) {
        std::cerr << "ServiceHostWin: Failed to initialize service host\n";
        std::cerr.flush();
        g_service_status.dwCurrentState = SERVICE_STOPPED;
        g_service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        g_service_status.dwCheckPoint = 0;
        SetServiceStatus(g_service_status_handle, &g_service_status);
        return;
    }
    
    // Report started status
    g_service_host_instance->report_status(SERVICE_RUNNING, NO_ERROR, 0);
    
    // Set up main loop to run agent core
    try {
        g_service_host_instance->run([config_path]() {
            ::run_agent_core(*g_service_host_static, config_path);
        });
        
        // Execute the main loop (run() stored it in main_loop_, now execute it)
        g_service_host_instance->run_service();
    } catch (const std::exception& e) {
        std::cerr << "ServiceHostWin: Exception in service main loop: " << e.what() << "\n";
        std::cerr.flush();
        if (g_service_status_handle) {
            g_service_status.dwCurrentState = SERVICE_STOPPED;
            g_service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            g_service_status.dwCheckPoint = 0;
            SetServiceStatus(g_service_status_handle, &g_service_status);
        }
        g_service_host_static.reset();
        g_service_host_instance = nullptr;
        return;
    } catch (...) {
        std::cerr << "ServiceHostWin: Unknown exception in service main loop\n";
        std::cerr.flush();
        if (g_service_status_handle) {
            g_service_status.dwCurrentState = SERVICE_STOPPED;
            g_service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            g_service_status.dwCheckPoint = 0;
            SetServiceStatus(g_service_status_handle, &g_service_status);
        }
        g_service_host_static.reset();
        g_service_host_instance = nullptr;
        return;
    }
    
    // Report stopped status
    if (g_service_host_instance) {
        g_service_host_instance->report_status(SERVICE_STOPPED, NO_ERROR, 0);
    } else if (g_service_status_handle) {
        g_service_status.dwCurrentState = SERVICE_STOPPED;
        g_service_status.dwWin32ExitCode = NO_ERROR;
        g_service_status.dwCheckPoint = 0;
        SetServiceStatus(g_service_status_handle, &g_service_status);
    }
    
    std::cout.flush();
    
    } catch (const std::exception& e) {
        // Report error status
        std::string errorMsg = "ServiceMain: Fatal exception: " + std::string(e.what());
        WriteEventLog(errorMsg, EVENTLOG_ERROR_TYPE);
        
        if (g_service_status_handle) {
            g_service_status.dwCurrentState = SERVICE_STOPPED;
            g_service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            g_service_status.dwServiceSpecificExitCode = 1;
            g_service_status.dwCheckPoint = 0;
            SetServiceStatus(g_service_status_handle, &g_service_status);
        }
        
        // Try to log error (may fail if log file wasn't set up)
        try {
            std::cerr << "ServiceHostWin: Fatal exception in ServiceMain: " << e.what() << "\n";
            std::cerr.flush();
        } catch (...) {
            // Ignore - can't log if logging failed
        }
        
    } catch (...) {
        // Report error status for unknown exception
        WriteEventLog("ServiceMain: Unknown fatal exception", EVENTLOG_ERROR_TYPE);
        
        if (g_service_status_handle) {
            g_service_status.dwCurrentState = SERVICE_STOPPED;
            g_service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            g_service_status.dwServiceSpecificExitCode = 2;
            g_service_status.dwCheckPoint = 0;
            SetServiceStatus(g_service_status_handle, &g_service_status);
        }
        
        // Try to log error
        try {
            std::cerr << "ServiceHostWin: Unknown fatal exception in ServiceMain\n";
            std::cerr.flush();
        } catch (...) {
            // Ignore
        }
    }
    
    // Clean up
    g_service_host_static.reset();
    g_service_host_instance = nullptr;
}

// Service control handler called by SCM
static DWORD WINAPI ServiceCtrlHandler(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
    if (!g_service_host_instance) {
        return ERROR_INVALID_FUNCTION;
    }
    
    switch (dwCtrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_service_host_instance->report_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            g_service_host_instance->shutdown();
            return NO_ERROR;
            
        case SERVICE_CONTROL_INTERROGATE:
            // SCM is querying status - already reported in report_status
            return NO_ERROR;
            
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

std::unique_ptr<ServiceHost> create_service_host() {
    return std::make_unique<ServiceHostWin>();
}

// Service entry point - called when running as Windows Service
// This should be called from main() when running as service
void run_as_service() {
    SERVICE_TABLE_ENTRY serviceTable[] = {
        { const_cast<LPSTR>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };
    
    // This call blocks until service stops
    if (!StartServiceCtrlDispatcher(serviceTable)) {
        DWORD error = GetLastError();
        std::cerr << "ServiceHostWin: Failed to start service dispatcher (error: " << error << ")\n";
        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            std::cerr << "ServiceHostWin: Not running as a service. Install as Windows Service or run in console mode.\n";
        }
    }
    
}

}  // namespace agent

#endif // _WIN32
