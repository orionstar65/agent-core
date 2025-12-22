#include "agent/extension_manager.hpp"
#include "agent/retry.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <limits.h>
#include <cerrno>
#include <thread>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace agent {

struct ExtensionState {
    ExtensionSpec spec;
    ExtState state{ExtState::Stopped};
#ifdef _WIN32
    DWORD pid{0};
    HANDLE handle{nullptr};
#else
    pid_t pid{0};
#endif
    int restart_count{0};
    std::chrono::steady_clock::time_point last_restart_time;
    std::chrono::steady_clock::time_point last_health_ping;
    std::chrono::steady_clock::time_point crash_time;
    std::chrono::steady_clock::time_point quarantine_start_time;
    std::chrono::steady_clock::time_point scheduled_restart_time;  // When to restart after crash (async delay)
    bool responding{false};
};

class ExtensionManagerImpl : public ExtensionManager {
public:
    explicit ExtensionManagerImpl(const Config::Extensions& config) : config_(config) {}
    ~ExtensionManagerImpl() { stop_all(); }

    void launch(const std::vector<ExtensionSpec>& specs) override {
        for (const auto& spec : specs) {
            if (!spec.enabled) continue;
            launch_single(spec);
        }
    }

    void stop_all() override {
        for (auto& [name, ext] : extensions_) {
            stop_single(name);
        }
        // Don't clear map - keep stopped extensions in status
    }

    void stop(const std::string& name) override {
        stop_single(name);
    }

    void monitor() override {
        auto now = std::chrono::steady_clock::now();
        for (auto& [name, ext] : extensions_) {
            if (ext.state == ExtState::Stopped) continue;

            if (ext.state == ExtState::Quarantined) {
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                    now - ext.quarantine_start_time).count();
                if (duration >= config_.quarantine_duration_s) {
                    ext.restart_count = 0;
                    // Save reset restart_count to map before launch_single, which will read from map
                    extensions_[name] = ext;
                    launch_single(ext.spec);
                }
                continue;
            }

            // Check if scheduled restart time has arrived
            // Only check if scheduled_restart_time was set (not default-initialized epoch)
            // We verify this by checking if scheduled_restart_time is after crash_time
            if (ext.state == ExtState::Crashed && 
                ext.scheduled_restart_time > ext.crash_time &&
                now >= ext.scheduled_restart_time) {
                // Time to restart - perform the restart now
                ext.last_restart_time = now;
                // Save last_restart_time to map before launch_single, which will read from map
                extensions_[name] = ext;
                launch_single(ext.spec);
                continue;
            }

            if (!is_alive(ext)) {
                // Reap zombie process before handling crash
                reap_zombie(ext);
                ext.state = ExtState::Crashed;
                ext.crash_time = now;
                handle_crash(ext);
            }
        }
    }

    void health_ping() override {
        auto now = std::chrono::steady_clock::now();
        for (auto& [name, ext] : extensions_) {
            if (ext.state == ExtState::Running) {
                ext.last_health_ping = now;
                ext.responding = is_alive(ext);
            }
        }
    }
    
    std::map<std::string, ExtState> status() const override {
        std::map<std::string, ExtState> result;
        for (const auto& [name, ext] : extensions_) {
            result[name] = ext.state;
        }
        return result;
    }

    std::map<std::string, ExtensionHealth> health_status() const override {
        std::map<std::string, ExtensionHealth> result;
        for (const auto& [name, ext] : extensions_) {
            ExtensionHealth h;
            h.name = name;
            h.state = ext.state;
            h.restart_count = ext.restart_count;
            h.last_health_ping = ext.last_health_ping;
            h.last_restart_time = ext.last_restart_time;
            h.crash_time = ext.crash_time;
            h.quarantine_start_time = ext.quarantine_start_time;
            h.responding = ext.responding;
            result[name] = h;
        }
        return result;
    }
    
    std::map<std::string, ProcessInfo> get_process_info() const override {
        std::map<std::string, ProcessInfo> info_map;
        
        for (const auto& [name, ext] : extensions_) {
            if (ext.state == ExtState::Running && ext.pid > 0) {
                ProcessInfo info;
                info.pid = static_cast<int>(ext.pid);
                info.executable_path = ext.spec.exec_path;
                
                // Extract executable name from path
                std::string path = ext.spec.exec_path;
#ifdef _WIN32
                size_t pos = path.find_last_of("\\/");
                if (pos != std::string::npos) {
                    info.executable_name = path.substr(pos + 1);
                } else {
                    info.executable_name = path;
                }
                // Remove .exe extension if present
                if (info.executable_name.size() > 4 && 
                    info.executable_name.substr(info.executable_name.size() - 4) == ".exe") {
                    info.executable_name = info.executable_name.substr(0, info.executable_name.size() - 4);
                }
#else
                size_t pos = path.find_last_of("/");
                if (pos != std::string::npos) {
                    info.executable_name = path.substr(pos + 1);
                } else {
                    info.executable_name = path;
                }
#endif
                info_map[name] = info;
            }
        }
        
        return info_map;
    }

private:
    Config::Extensions config_;
    std::map<std::string, ExtensionState> extensions_;

    void launch_single(const ExtensionSpec& spec) {
        // Check if extension already exists to preserve restart count
        auto it = extensions_.find(spec.name);
        ExtensionState ext;
        if (it != extensions_.end()) {
            ext = it->second;  // Preserve existing state including restart_count
#ifdef _WIN32
            // Close old process handle before overwriting with new one to prevent handle leak
            if (ext.handle != nullptr && ext.handle != INVALID_HANDLE_VALUE) {
                CloseHandle(ext.handle);
                ext.handle = nullptr;
            }
#endif
        } else {
            ext.spec = spec;
        }
        // Always update spec to reflect current specification (may have changed args, etc.)
        ext.spec = spec;
        ext.state = ExtState::Starting;

#ifdef _WIN32
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(si);
        std::string cmd = spec.exec_path;
        for (const auto& arg : spec.args) cmd += " " + arg;
        std::vector<char> buf(cmd.begin(), cmd.end());
        buf.push_back('\0');
        if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            ext.pid = pi.dwProcessId;
            ext.handle = pi.hProcess;
            ext.state = ExtState::Running;
            CloseHandle(pi.hThread);
        } else {
            ext.state = ExtState::Crashed;
        }
#else
        char resolved[PATH_MAX];
        if (realpath(spec.exec_path.c_str(), resolved) == nullptr) {
            ext.state = ExtState::Crashed;
            extensions_[spec.name] = ext;
            return;
        }
        pid_t pid = fork();
        if (pid == 0) {
            std::vector<char*> argv;
            argv.push_back(resolved);
            for (const auto& arg : spec.args) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);
            execv(resolved, argv.data());
            _exit(1);
        } else if (pid > 0) {
            ext.pid = pid;
            ext.state = ExtState::Running;
        } else {
            ext.state = ExtState::Crashed;
        }
#endif
        extensions_[spec.name] = ext;
    }

    void stop_single(const std::string& name) {
        auto it = extensions_.find(name);
        if (it == extensions_.end() || it->second.state == ExtState::Stopped) return;
        auto& ext = it->second;
#ifdef _WIN32
        if (ext.handle) {
            TerminateProcess(ext.handle, 0);
            WaitForSingleObject(ext.handle, 5000);
            CloseHandle(ext.handle);
            ext.handle = nullptr;
        }
#else
        if (ext.pid > 0) {
            kill(ext.pid, SIGTERM);
            int status;
            waitpid(ext.pid, &status, 0);
            ext.pid = 0;
        }
#endif
        ext.state = ExtState::Stopped;
    }

    bool is_alive(ExtensionState& ext) {
#ifdef _WIN32
        if (!ext.handle) return false;
        DWORD code;
        return GetExitCodeProcess(ext.handle, &code) && code == STILL_ACTIVE;
#else
        if (ext.pid <= 0) return false;
        
        // Use kill(pid, 0) to check if process exists without reaping zombies
        // This is non-destructive and can be called safely by both health_ping() and monitor()
        if (kill(ext.pid, 0) != 0) {
            // Process doesn't exist (errno will be ESRCH)
            return false;
        }
        
        // Process exists, but could be a zombie. Check /proc/{pid}/stat to see state
        // State is the 3rd field in /proc/{pid}/stat
        std::string stat_path = "/proc/" + std::to_string(ext.pid) + "/stat";
        std::ifstream stat_file(stat_path);
        if (!stat_file) {
            // Can't read stat file, assume process is dead
            return false;
        }
        
        // Read the stat file to get process state
        std::string line;
        std::getline(stat_file, line);
        std::istringstream iss(line);
        std::string pid_str, comm, state;
        iss >> pid_str >> comm >> state;
        
        // State 'Z' means zombie, any other state means process is alive
        // (R=running, S=sleeping, D=disk sleep, etc.)
        return state != "Z";
#endif
    }
    
    // Separate function to reap zombie processes (only called when we know process is dead)
    void reap_zombie(ExtensionState& ext) {
#ifndef _WIN32
        if (ext.pid <= 0) return;
        
        // Reap zombie process if it exists
        int status;
        pid_t result = waitpid(ext.pid, &status, WNOHANG);
        if (result == ext.pid) {
            // Zombie was reaped
            ext.pid = 0;
        }
#endif
    }

    void handle_crash(ExtensionState& ext) {
        ext.restart_count++;
        
        if (ext.restart_count >= config_.max_restart_attempts) {
            std::cerr << "ExtensionManager: " << ext.spec.name << " quarantined after "
                      << ext.restart_count << " crashes\n";
            ext.state = ExtState::Quarantined;
            ext.quarantine_start_time = std::chrono::steady_clock::now();
            // Save state to map before returning
            extensions_[ext.spec.name] = ext;
            return;
        }
        // Save incremented restart_count to map before scheduling restart
        extensions_[ext.spec.name] = ext;
        
        // calculate_backoff_with_jitter expects 0-based attempt number
        // restart_count is 1-based (1 = first crash, 2 = second crash, etc.)
        // So we pass restart_count - 1 (0 = first restart attempt, 1 = second restart attempt, etc.)
        int delay = calculate_backoff_with_jitter(
            ext.restart_count - 1, config_.restart_base_delay_ms, config_.restart_max_delay_ms, 20);
        
        // Schedule restart time asynchronously instead of blocking
        // monitor() will check this time and perform the restart when ready
        ext.scheduled_restart_time = std::chrono::steady_clock::now() + 
            std::chrono::milliseconds(delay);
        
        // Save scheduled restart time to map
        extensions_[ext.spec.name] = ext;
    }
};

std::unique_ptr<ExtensionManager> create_extension_manager(const Config::Extensions& config) {
    return std::make_unique<ExtensionManagerImpl>(config);
}

}
