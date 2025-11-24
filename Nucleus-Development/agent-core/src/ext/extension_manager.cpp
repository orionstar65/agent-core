#include "agent/extension_manager.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <limits.h>
#include <cerrno>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace agent {

class ExtensionManagerImpl : public ExtensionManager {
public:
    void launch(const std::vector<ExtensionSpec>& specs) override {
        std::cout << "ExtensionManager: Launching " << specs.size() << " extensions\n";
        
        for (const auto& spec : specs) {
            std::cout << "  - Extension: " << spec.name
                      << ", Exec: " << spec.exec_path
                      << ", Critical: " << (spec.critical ? "yes" : "no") << "\n";
            
#ifdef _WIN32
            STARTUPINFOA si = {0};
            PROCESS_INFORMATION pi = {0};
            si.cb = sizeof(si);
            
            // Build command line with proper quoting for paths with spaces
            std::string cmd_line;
            if (!spec.exec_path.empty() && (spec.exec_path.find(' ') != std::string::npos || spec.exec_path.find('\t') != std::string::npos)) {
                cmd_line = "\"" + spec.exec_path + "\"";
            } else {
                cmd_line = spec.exec_path;
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
                process_ids_[spec.name] = pi.dwProcessId;
                states_[spec.name] = ExtState::Running;
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            } else {
                DWORD error = GetLastError();
                std::cerr << "ExtensionManager: Failed to launch " << spec.name 
                          << " (error: " << error << ")\n";
                states_[spec.name] = ExtState::Crashed;
            }
#else
            char resolved_path[PATH_MAX];
            if (realpath(spec.exec_path.c_str(), resolved_path) == nullptr) {
                std::cerr << "ExtensionManager: Failed to resolve path: " << spec.exec_path 
                          << " (errno: " << errno << ")\n";
                states_[spec.name] = ExtState::Crashed;
                continue;
            }
            
            pid_t pid = fork();
            if (pid == 0) {
                std::vector<char*> argv;
                argv.reserve(spec.args.size() + 2);
                argv.push_back(const_cast<char*>(resolved_path));
                for (const auto& arg : spec.args) {
                    argv.push_back(const_cast<char*>(arg.c_str()));
                }
                argv.push_back(nullptr);
                
                execv(resolved_path, argv.data());
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
            
            auto it = process_ids_.find(name);
            if (it != process_ids_.end()) {
#ifdef _WIN32
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, it->second);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
#else
                kill(it->second, SIGTERM);
                int status;
                waitpid(it->second, &status, 0);
#endif
                process_ids_.erase(it);
            }
            state = ExtState::Stopped;
        }
    }
    
    std::map<std::string, ExtState> status() const override {
        return states_;
    }

private:
    std::map<std::string, ExtState> states_;
#ifdef _WIN32
    std::map<std::string, DWORD> process_ids_;
#else
    std::map<std::string, pid_t> process_ids_;
#endif
};

std::unique_ptr<ExtensionManager> create_extension_manager() {
    return std::make_unique<ExtensionManagerImpl>();
}

}
