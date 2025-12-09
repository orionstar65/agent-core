#include "agent/extension_manager.hpp"
#include "agent/path_utils.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cerrno>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#endif

namespace agent {

class ExtensionManagerImpl : public ExtensionManager {
public:
    ~ExtensionManagerImpl() {
        // Clean up Windows process handles
#ifdef _WIN32
        for (auto& [name, handle] : process_handles_) {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
        }
        process_handles_.clear();
#endif
    }
    
    void launch(const std::vector<ExtensionSpec>& specs) override {
        std::cout << "ExtensionManager: Launching " << specs.size() << " extensions\n";
        
        for (const auto& spec : specs) {
            std::cout << "  - Extension: " << spec.name
                      << ", Exec: " << spec.exec_path
                      << ", Critical: " << (spec.critical ? "yes" : "no") << "\n";
            
#ifdef _WIN32
            // Resolve path using cross-platform utility
            std::string resolved_path = util::resolve_path(spec.exec_path);
            if (resolved_path.empty()) {
                std::cerr << "ExtensionManager: Failed to resolve path: " << spec.exec_path << "\n";
                states_[spec.name] = ExtState::Crashed;
                continue;
            }
            
            STARTUPINFOA si = {0};
            PROCESS_INFORMATION pi = {0};
            si.cb = sizeof(si);
            
            // Build command line with proper quoting for paths with spaces
            std::string cmd_line;
            if (!resolved_path.empty() && (resolved_path.find(' ') != std::string::npos || resolved_path.find('\t') != std::string::npos)) {
                cmd_line = "\"" + resolved_path + "\"";
            } else {
                cmd_line = resolved_path;
            }
            for (const auto& arg : spec.args) {
                cmd_line += " ";
                if (!arg.empty() && (arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos)) {
                    cmd_line += "\"" + arg + "\"";
                } else {
                    cmd_line += arg;
                }
            }
            
            // CreateProcessA requires non-const buffer (it may modify it)
            std::vector<char> cmd_line_buf(cmd_line.begin(), cmd_line.end());
            cmd_line_buf.push_back('\0');
            
            if (CreateProcessA(nullptr, cmd_line_buf.data(),
                             nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                // Close any existing handle for this name to prevent handle leak
                auto existing_handle_it = process_handles_.find(spec.name);
                if (existing_handle_it != process_handles_.end() && 
                    existing_handle_it->second != nullptr && 
                    existing_handle_it->second != INVALID_HANDLE_VALUE) {
                    CloseHandle(existing_handle_it->second);
                }
                
                // Keep process handle open for monitoring
                process_handles_[spec.name] = pi.hProcess;
                process_ids_[spec.name] = pi.dwProcessId;
                states_[spec.name] = ExtState::Running;
                CloseHandle(pi.hThread);  // Thread handle not needed
            } else {
                DWORD error = GetLastError();
                std::cerr << "ExtensionManager: Failed to launch " << spec.name 
                          << " (error: " << error << ")\n";
                states_[spec.name] = ExtState::Crashed;
            }
#else
            // Resolve path using cross-platform utility
            std::string resolved_path = util::resolve_path(spec.exec_path);
            if (resolved_path.empty()) {
                std::cerr << "ExtensionManager: Failed to resolve path: " << spec.exec_path 
                          << " (errno: " << errno << ")\n";
                states_[spec.name] = ExtState::Crashed;
                continue;
            }
            
            pid_t pid = fork();
            if (pid == 0) {
                std::vector<char*> argv;
                argv.reserve(spec.args.size() + 2);
                argv.push_back(const_cast<char*>(resolved_path.c_str()));
                for (const auto& arg : spec.args) {
                    argv.push_back(const_cast<char*>(arg.c_str()));
                }
                argv.push_back(nullptr);
                
                execv(resolved_path.c_str(), argv.data());
                std::cerr << "ExtensionManager: Failed to exec: " << resolved_path << "\n";
                _exit(1);
            } else if (pid > 0) {
                process_ids_[spec.name] = pid;
                states_[spec.name] = ExtState::Running;
            } else {
                std::cerr << "ExtensionManager: Failed to fork for: " << spec.name << "\n";
                states_[spec.name] = ExtState::Crashed;
            }
#endif
        }
    }
    
    void stop_all() override {
        std::cout << "ExtensionManager: Stopping all extensions\n";
        
        for (auto& [name, state] : states_) {
            std::cout << "  - Stopping: " << name << "\n";
            
#ifdef _WIN32
            auto handle_it = process_handles_.find(name);
            if (handle_it != process_handles_.end() && handle_it->second != nullptr && handle_it->second != INVALID_HANDLE_VALUE) {
                TerminateProcess(handle_it->second, 0);
                CloseHandle(handle_it->second);
                process_handles_.erase(handle_it);
            }
            auto id_it = process_ids_.find(name);
            if (id_it != process_ids_.end()) {
                process_ids_.erase(id_it);
            }
#else
            auto it = process_ids_.find(name);
            if (it != process_ids_.end()) {
                kill(it->second, SIGTERM);
                int status;
                waitpid(it->second, &status, 0);
                process_ids_.erase(it);
            }
#endif
            state = ExtState::Stopped;
        }
    }
    
    std::map<std::string, ExtState> status() override {
        // Update states based on process status
#ifdef _WIN32
        for (auto& [name, state] : states_) {
            if (state == ExtState::Running) {
                auto handle_it = process_handles_.find(name);
                if (handle_it != process_handles_.end() && handle_it->second != nullptr && handle_it->second != INVALID_HANDLE_VALUE) {
                    DWORD exit_code;
                    if (GetExitCodeProcess(handle_it->second, &exit_code)) {
                        if (exit_code != STILL_ACTIVE) {
                            state = ExtState::Crashed;
                        }
                    } else {
                        // Process handle invalid, assume crashed
                        state = ExtState::Crashed;
                    }
                } else {
                    state = ExtState::Crashed;
                }
            }
        }
#endif
        return states_;
    }

private:
    std::map<std::string, ExtState> states_;
#ifdef _WIN32
    std::map<std::string, HANDLE> process_handles_;  // Keep handles for monitoring
    std::map<std::string, DWORD> process_ids_;        // Keep IDs for reference
#else
    std::map<std::string, pid_t> process_ids_;
#endif
};

std::unique_ptr<ExtensionManager> create_extension_manager() {
    return std::make_unique<ExtensionManagerImpl>();
}

}
