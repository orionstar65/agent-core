#include "agent/extension_manager.hpp"
#include "agent/retry.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <limits.h>
#include <cerrno>
#include <thread>

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
                    launch_single(ext.spec);
                }
                continue;
            }

            if (!is_alive(ext)) {
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

private:
    Config::Extensions config_;
    std::map<std::string, ExtensionState> extensions_;

    void launch_single(const ExtensionSpec& spec) {
        // Check if extension already exists to preserve restart count
        auto it = extensions_.find(spec.name);
        ExtensionState ext;
        if (it != extensions_.end()) {
            ext = it->second;  // Preserve existing state including restart_count
        } else {
            ext.spec = spec;
        }
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
        
        // Use waitpid with WNOHANG to check if process has exited
        // This also reaps zombie processes
        int status;
        pid_t result = waitpid(ext.pid, &status, WNOHANG);
        
        if (result == 0) {
            // Process still running
            return true;
        } else if (result == ext.pid) {
            // Process has exited (zombie reaped)
            return false;
        } else {
            // Error (probably no such process)
            return false;
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
        // Save incremented restart_count to map before restart
        extensions_[ext.spec.name] = ext;
        
        int delay = calculate_backoff_with_jitter(
            ext.restart_count, config_.restart_base_delay_ms, config_.restart_max_delay_ms, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        ext.last_restart_time = std::chrono::steady_clock::now();
        launch_single(ext.spec);
    }
};

std::unique_ptr<ExtensionManager> create_extension_manager(const Config::Extensions& config) {
    return std::make_unique<ExtensionManagerImpl>(config);
}

}
