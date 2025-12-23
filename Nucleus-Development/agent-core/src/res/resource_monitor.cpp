#include "agent/resource_monitor.hpp"
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <string>
#endif

namespace agent {

class ResourceMonitorImpl : public ResourceMonitor {
public:
    ResourceMonitorImpl() {
#ifdef _WIN32
        // Initialize CPU time tracking
        process_cpu_times_ = std::map<int, ProcessCpuTime>();
#else
        // Initialize previous CPU times for Linux
        prev_cpu_times_ = std::map<int, std::pair<int64_t, uint64_t>>();
        prev_system_cpu_time_ = 0;
        prev_system_idle_time_ = 0;
#endif
    }
    
    ~ResourceMonitorImpl() = default;
    
    ResourceUsage sample(const std::string& process_name) const override {
        // Special case: "agent-core" refers to the current process
        if (process_name == "agent-core") {
            return sample_by_pid(get_current_pid());
        }
        
        // Find PID by process name
        int pid = find_pid_by_name(process_name);
        if (pid > 0) {
            return sample_by_pid(pid);
        }
        
        // Process not found - return empty usage
        return ResourceUsage();
    }
    
    ResourceUsage sample_by_pid(int pid) const override {
        ResourceUsage usage;
        
        if (pid <= 0) {
            return usage;
        }
        
#ifdef _WIN32
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) {
            return usage;
        }
        
        // Memory
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            usage.mem_mb = pmc.WorkingSetSize / (1024 * 1024);
        }
        
        // CPU
        FILETIME creation_time, exit_time, kernel_time, user_time;
        if (GetProcessTimes(hProcess, &creation_time, &exit_time, &kernel_time, &user_time)) {
            ULARGE_INTEGER ktime, utime;
            ktime.LowPart = kernel_time.dwLowDateTime;
            ktime.HighPart = kernel_time.dwHighDateTime;
            utime.LowPart = user_time.dwLowDateTime;
            utime.HighPart = user_time.dwHighDateTime;
            
            auto now = std::chrono::steady_clock::now();
            auto it = process_cpu_times_.find(pid);
            if (it != process_cpu_times_.end()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second.last_time).count();
                if (elapsed > 0) {
                    uint64_t total_time = (ktime.QuadPart + utime.QuadPart) - it->second.last_total;
                    // Convert 100-nanosecond intervals to percentage
                    double cpu_time_ms = (total_time / 10000.0);
                    usage.cpu_pct = (cpu_time_ms / elapsed) * 100.0;
                    usage.cpu_pct = std::min(100.0, std::max(0.0, usage.cpu_pct));
                }
            }
            
            ProcessCpuTime pct;
            pct.last_time = now;
            pct.last_total = ktime.QuadPart + utime.QuadPart;
            process_cpu_times_[pid] = pct;
        }
        
        // Handles
        DWORD handle_count = 0;
        GetProcessHandleCount(hProcess, &handle_count);
        usage.handles = handle_count;
        
        // Network and disk I/O would require WMI or other APIs
        // For now, set to 0 (can be enhanced later)
        usage.net_in_kbps = 0;
        usage.net_out_kbps = 0;
        usage.disk_read_mb = 0;
        usage.disk_write_mb = 0;
        
        CloseHandle(hProcess);
#else
        // Linux implementation using /proc filesystem
        std::string proc_path = "/proc/" + std::to_string(pid);
        
        // Memory from /proc/pid/status
        std::ifstream status_file(proc_path + "/status");
        if (status_file.is_open()) {
            std::string line;
            while (std::getline(status_file, line)) {
                if (line.find("VmRSS:") == 0) {
                    std::istringstream iss(line);
                    std::string key, value, unit;
                    iss >> key >> value >> unit;
                    if (unit == "kB") {
                        usage.mem_mb = std::stoll(value) / 1024;
                    }
                    break;
                }
            }
            status_file.close();
        }
        
        // CPU from /proc/pid/stat
        // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
        // The comm field (field 2) is in parentheses and can contain spaces, so we need special parsing
        std::ifstream stat_file(proc_path + "/stat");
        if (stat_file.is_open()) {
            std::string line;
            std::getline(stat_file, line);
            
            // Find the command name in parentheses (field 2)
            size_t paren_start = line.find('(');
            size_t paren_end = line.find(')', paren_start);
            
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                // Extract fields before command name (pid)
                std::string pid_str = line.substr(0, paren_start);
                // Trim whitespace from pid_str
                while (!pid_str.empty() && std::isspace(pid_str.back())) {
                    pid_str.pop_back();
                }
                // Skip the command name and closing paren, then parse remaining fields
                std::string remaining = line.substr(paren_end + 1);
                // Trim leading whitespace from remaining
                while (!remaining.empty() && std::isspace(remaining.front())) {
                    remaining.erase(0, 1);
                }
                std::istringstream iss(remaining);
                
                // Now parse: state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
                std::string state, ppid, pgrp, session, tty_nr, tpgid, flags;
                std::string minflt, cminflt, majflt, cmajflt, utime_str, stime_str;
                iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags;
                iss >> minflt >> cminflt >> majflt >> cmajflt >> utime_str >> stime_str;
                
                if (!utime_str.empty() && !stime_str.empty()) {
                    uint64_t utime = std::stoull(utime_str);
                    uint64_t stime = std::stoull(stime_str);
                    uint64_t total_time = utime + stime;
                
                auto now = std::chrono::steady_clock::now();
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                
                auto it = prev_cpu_times_.find(pid);
                if (it != prev_cpu_times_.end()) {
                    int64_t elapsed = now_ms - it->second.first;
                    if (elapsed > 0 && it->second.second > 0) {
                        uint64_t time_diff = total_time - it->second.second;
                        // Convert jiffies to seconds (assuming 100 HZ clock ticks per second)
                        // Then calculate percentage: (cpu_time / elapsed_time) * 100
                        double cpu_time_sec = time_diff / 100.0;
                        double elapsed_sec = elapsed / 1000.0;
                        if (elapsed_sec > 0) {
                            usage.cpu_pct = (cpu_time_sec / elapsed_sec) * 100.0;
                            usage.cpu_pct = std::min(100.0, std::max(0.0, usage.cpu_pct));
                        }
                    }
                }
                
                prev_cpu_times_[pid] = std::make_pair(now_ms, total_time);
                }
            }
            stat_file.close();
        }
        
        // File descriptors from /proc/pid/fd
        DIR* dir = opendir((proc_path + "/fd").c_str());
        if (dir) {
            int fd_count = 0;
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_name[0] != '.') {
                    fd_count++;
                }
            }
            closedir(dir);
            usage.handles = fd_count;
        }
        
        // Disk I/O from /proc/pid/io
        std::ifstream io_file(proc_path + "/io");
        if (io_file.is_open()) {
            std::string line;
            while (std::getline(io_file, line)) {
                if (line.find("read_bytes:") == 0) {
                    std::istringstream iss(line);
                    std::string key, value;
                    iss >> key >> value;
                    usage.disk_read_mb = std::stoll(value) / (1024 * 1024);
                } else if (line.find("write_bytes:") == 0) {
                    std::istringstream iss(line);
                    std::string key, value;
                    iss >> key >> value;
                    usage.disk_write_mb = std::stoll(value) / (1024 * 1024);
                }
            }
            io_file.close();
        }
        
        // Network I/O would require /proc/net/dev parsing (system-wide)
        // For per-process, would need netfilter or eBPF
        usage.net_in_kbps = 0;
        usage.net_out_kbps = 0;
#endif
        
        return usage;
    }
    
    ResourceUsage sample_system() const override {
        ResourceUsage usage;
        
#ifdef _WIN32
        // System memory
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        usage.mem_mb = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
        
        // System CPU (simplified - would use PDH for accurate measurement)
        usage.cpu_pct = 0.0; // Placeholder
        
        // System network - use GetIpStatisticsEx for basic stats
        MIB_IPSTATS ip_stats;
        if (GetIpStatisticsEx(&ip_stats, AF_INET) == NO_ERROR) {
            // Calculate rate over time window
            auto now = std::chrono::steady_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            // Note: GetIpStatisticsEx doesn't provide byte counts directly
            // For a more accurate implementation, we'd use GetIfTable/GetIfEntry
            // For now, we'll use a simplified approach
            if (prev_net_time_ > 0) {
                int64_t elapsed_ms = now_ms - prev_net_time_;
                // Placeholder: would need to track actual bytes from interfaces
                usage.net_in_kbps = 0;
                usage.net_out_kbps = 0;
            }
            
            prev_net_time_ = now_ms;
        } else {
            usage.net_in_kbps = 0;
            usage.net_out_kbps = 0;
        }
        
        usage.disk_read_mb = 0;
        usage.disk_write_mb = 0;
        usage.handles = 0;
#else
        // System memory from /proc/meminfo
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open()) {
            std::string line;
            uint64_t mem_total = 0, mem_available = 0;
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    std::istringstream iss(line);
                    std::string key, value, unit;
                    iss >> key >> value >> unit;
                    mem_total = std::stoull(value);
                } else if (line.find("MemAvailable:") == 0) {
                    std::istringstream iss(line);
                    std::string key, value, unit;
                    iss >> key >> value >> unit;
                    mem_available = std::stoull(value);
                }
            }
            meminfo.close();
            usage.mem_mb = (mem_total - mem_available) / 1024;
        }
        
        // System CPU from /proc/stat
        std::ifstream stat_file("/proc/stat");
        if (stat_file.is_open()) {
            std::string line;
            std::getline(stat_file, line);
            std::istringstream iss(line);
            std::string cpu;
            uint64_t user, nice, system, idle, iowait, irq, softirq;
            iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
            
            uint64_t total_time = user + nice + system + idle + iowait + irq + softirq;
            uint64_t idle_time = idle + iowait;
            
            auto now = std::chrono::steady_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            if (prev_system_cpu_time_ > 0 && prev_system_idle_time_ > 0) {
                uint64_t total_diff = total_time - prev_system_cpu_time_;
                uint64_t idle_diff = idle_time - prev_system_idle_time_;
                if (total_diff > 0) {
                    usage.cpu_pct = 100.0 * (1.0 - (double)idle_diff / total_diff);
                    usage.cpu_pct = std::min(100.0, std::max(0.0, usage.cpu_pct));
                }
            }
            
            prev_system_cpu_time_ = total_time;
            prev_system_idle_time_ = idle_time;
            prev_system_time_ = now_ms;
            
            stat_file.close();
        }
        
        // System network from /proc/net/dev
        std::ifstream net_file("/proc/net/dev");
        if (net_file.is_open()) {
            std::string line;
            uint64_t rx_bytes = 0, tx_bytes = 0;
            // Skip header lines
            std::getline(net_file, line);
            std::getline(net_file, line);
            while (std::getline(net_file, line)) {
                std::istringstream iss(line);
                std::string iface;
                uint64_t rx, tx;
                iss >> iface >> rx;
                // Skip other fields to get tx
                for (int i = 0; i < 7; i++) {
                    std::string dummy;
                    iss >> dummy;
                }
                iss >> tx;
                rx_bytes += rx;
                tx_bytes += tx;
            }
            net_file.close();
            
            // Calculate rate over time window
            auto now = std::chrono::steady_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            if (prev_net_time_ > 0 && prev_net_rx_bytes_ > 0 && prev_net_tx_bytes_ > 0) {
                int64_t elapsed_ms = now_ms - prev_net_time_;
                if (elapsed_ms > 0) {
                    uint64_t rx_diff = rx_bytes - prev_net_rx_bytes_;
                    uint64_t tx_diff = tx_bytes - prev_net_tx_bytes_;
                    
                    // Convert bytes to KB and calculate rate per second
                    double elapsed_sec = elapsed_ms / 1000.0;
                    usage.net_in_kbps = static_cast<int64_t>((rx_diff / 1024.0) / elapsed_sec);
                    usage.net_out_kbps = static_cast<int64_t>((tx_diff / 1024.0) / elapsed_sec);
                }
            }
            
            prev_net_rx_bytes_ = rx_bytes;
            prev_net_tx_bytes_ = tx_bytes;
            prev_net_time_ = now_ms;
        }
        
        usage.disk_read_mb = 0;
        usage.disk_write_mb = 0;
        usage.handles = 0;
#endif
        
        return usage;
    }
    
    bool exceeds_budget(const ResourceUsage& usage, const Config& config) const override {
        bool exceeds = false;
        
        if (usage.cpu_pct > config.resource.cpu_max_pct) {
            std::cout << "ResourceMonitor: CPU exceeds budget (" 
                      << usage.cpu_pct << "% > " << config.resource.cpu_max_pct << "%)\n";
            exceeds = true;
        }
        
        if (usage.mem_mb > config.resource.mem_max_mb) {
            std::cout << "ResourceMonitor: Memory exceeds budget ("
                      << usage.mem_mb << "MB > " << config.resource.mem_max_mb << "MB)\n";
            exceeds = true;
        }
        
        int64_t total_net = usage.net_in_kbps + usage.net_out_kbps;
        if (total_net > config.resource.net_max_kbps) {
            std::cout << "ResourceMonitor: Network exceeds budget ("
                      << total_net << "KB/s > " << config.resource.net_max_kbps << "KB/s)\n";
            exceeds = true;
        }
        
        return exceeds;
    }
    
    bool set_cpu_priority(int pid, int priority) const override {
        if (pid <= 0) return false;
        
#ifdef _WIN32
        HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hProcess) {
            return false;
        }
        
        DWORD priority_class;
        // priority: 0 = normal, 1 = below normal, 2 = idle
        if (priority == 0) {
            priority_class = NORMAL_PRIORITY_CLASS;
        } else if (priority == 1) {
            priority_class = BELOW_NORMAL_PRIORITY_CLASS;
        } else {
            priority_class = IDLE_PRIORITY_CLASS;
        }
        
        bool result = SetPriorityClass(hProcess, priority_class) != 0;
        CloseHandle(hProcess);
        return result;
#else
        // Linux: nice values (higher = lower priority)
        // priority: 0 = normal (nice 0), 1 = below normal (nice 5), 2 = idle (nice 19)
        int nice_value = 0;
        if (priority == 1) {
            nice_value = 5;
        } else if (priority >= 2) {
            nice_value = 19;
        }
        
        return setpriority(PRIO_PROCESS, pid, nice_value) == 0;
#endif
    }
    
    bool set_memory_limit(int pid, int64_t max_mb) const override {
        if (pid <= 0 || max_mb <= 0) return false;
        
#ifdef _WIN32
        // Windows: Use Job Objects for memory limits
        // Note: This requires creating a job object and assigning the process to it
        // For simplicity, we'll use SetProcessWorkingSetSize as a soft limit
        HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!hProcess) {
            return false;
        }
        
        SIZE_T min_ws = 0;
        SIZE_T max_ws = static_cast<SIZE_T>(max_mb * 1024 * 1024);
        bool result = SetProcessWorkingSetSize(hProcess, min_ws, max_ws) != 0;
        CloseHandle(hProcess);
        return result;
#else
        // Linux: Use setrlimit
        struct rlimit rl;
        if (getrlimit(RLIMIT_AS, &rl) != 0) {
            return false;
        }
        
        rl.rlim_cur = static_cast<rlim_t>(max_mb * 1024 * 1024);
        rl.rlim_max = static_cast<rlim_t>(max_mb * 1024 * 1024);
        
        // Note: setrlimit requires the process to call it on itself or have appropriate privileges
        // For external processes, we'd need to use prlimit (if available) or cgroups
        // For now, return false as we can't set limits on other processes easily
        return false;
#endif
    }
    
    bool reset_limits(int pid) const override {
        if (pid <= 0) return false;
        
#ifdef _WIN32
        HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hProcess) {
            return false;
        }
        
        bool result = SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS) != 0;
        CloseHandle(hProcess);
        return result;
#else
        return setpriority(PRIO_PROCESS, pid, 0) == 0;
#endif
    }
    
    ResourceUsage aggregate_usage(const std::vector<int>& pids) const override {
        ResourceUsage total;
        
        for (int pid : pids) {
            if (pid <= 0) continue;
            auto usage = sample_by_pid(pid);
            total.cpu_pct += usage.cpu_pct;
            total.mem_mb += usage.mem_mb;
            total.net_in_kbps += usage.net_in_kbps;
            total.net_out_kbps += usage.net_out_kbps;
            total.disk_read_mb += usage.disk_read_mb;
            total.disk_write_mb += usage.disk_write_mb;
            total.handles += usage.handles;
        }
        
        // Note: CPU percentage is per-core, so aggregate can exceed 100% on multi-core systems
        // For example, 2 processes each using 100% CPU on a 4-core system = 200% aggregate
        // No capping needed - the sum represents total CPU usage across all cores
        
        return total;
    }

private:
    int get_current_pid() const {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return getpid();
#endif
    }
    
    int find_pid_by_name(const std::string& process_name) const {
        if (process_name.empty()) {
            return 0;
        }
        
#ifdef _WIN32
        // Windows: Use CreateToolhelp32Snapshot to enumerate processes
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return 0;
        }
        
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        
        if (Process32First(hSnapshot, &pe32)) {
            do {
                std::string exe_name = pe32.szExeFile;
                // Remove .exe extension if present for comparison
                if (exe_name.size() > 4 && 
                    exe_name.substr(exe_name.size() - 4) == ".exe") {
                    exe_name = exe_name.substr(0, exe_name.size() - 4);
                }
                
                // Compare with process_name (case-insensitive on Windows)
                std::string exe_lower = exe_name;
                std::string proc_lower = process_name;
                std::transform(exe_lower.begin(), exe_lower.end(), exe_lower.begin(), ::tolower);
                std::transform(proc_lower.begin(), proc_lower.end(), proc_lower.begin(), ::tolower);
                if (exe_lower == proc_lower) {
                    CloseHandle(hSnapshot);
                    return static_cast<int>(pe32.th32ProcessID);
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        
        CloseHandle(hSnapshot);
        return 0;
#else
        // Linux: Read /proc directory to find processes by name
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) {
            return 0;
        }
        
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            // Check if entry is a PID directory (numeric)
            if (entry->d_type != DT_DIR) {
                continue;
            }
            
            bool is_numeric = true;
            for (int i = 0; entry->d_name[i] != '\0'; i++) {
                if (!std::isdigit(entry->d_name[i])) {
                    is_numeric = false;
                    break;
                }
            }
            
            if (!is_numeric) {
                continue;
            }
            
            int pid = std::stoi(entry->d_name);
            
            // Read /proc/PID/comm or /proc/PID/cmdline to get process name
            std::string comm_path = "/proc/" + std::string(entry->d_name) + "/comm";
            std::ifstream comm_file(comm_path);
            if (comm_file.is_open()) {
                std::string proc_name;
                std::getline(comm_file, proc_name);
                // Remove trailing newline if present
                if (!proc_name.empty() && proc_name.back() == '\n') {
                    proc_name.pop_back();
                }
                
                if (proc_name == process_name) {
                    closedir(proc_dir);
                    return pid;
                }
            }
            
            // Fallback: try cmdline (first argument)
            std::string cmdline_path = "/proc/" + std::string(entry->d_name) + "/cmdline";
            std::ifstream cmdline_file(cmdline_path);
            if (cmdline_file.is_open()) {
                std::string cmdline;
                std::getline(cmdline_file, cmdline);
                if (!cmdline.empty()) {
                    // Extract executable name from path
                    size_t last_slash = cmdline.find_last_of('/');
                    std::string exe_name = (last_slash != std::string::npos) 
                        ? cmdline.substr(last_slash + 1) 
                        : cmdline;
                    
                    // Remove null terminators (cmdline uses null-separated args)
                    size_t null_pos = exe_name.find('\0');
                    if (null_pos != std::string::npos) {
                        exe_name = exe_name.substr(0, null_pos);
                    }
                    
                    if (exe_name == process_name) {
                        closedir(proc_dir);
                        return pid;
                    }
                }
            }
        }
        
        closedir(proc_dir);
        return 0;
#endif
    }
    
#ifdef _WIN32
    struct ProcessCpuTime {
        std::chrono::steady_clock::time_point last_time;
        uint64_t last_total;
    };
    mutable std::map<int, ProcessCpuTime> process_cpu_times_;
    mutable uint64_t prev_net_rx_bytes_{0};
    mutable uint64_t prev_net_tx_bytes_{0};
    mutable int64_t prev_net_time_{0};
#else
    mutable std::map<int, std::pair<int64_t, uint64_t>> prev_cpu_times_;
    mutable uint64_t prev_system_cpu_time_{0};
    mutable uint64_t prev_system_idle_time_{0};
    mutable int64_t prev_system_time_{0};
    mutable uint64_t prev_net_rx_bytes_{0};
    mutable uint64_t prev_net_tx_bytes_{0};
    mutable int64_t prev_net_time_{0};
#endif
};

std::unique_ptr<ResourceMonitor> create_resource_monitor() {
    return std::make_unique<ResourceMonitorImpl>();
}

}
