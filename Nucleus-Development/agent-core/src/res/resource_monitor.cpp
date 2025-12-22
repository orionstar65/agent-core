#include "agent/resource_monitor.hpp"
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
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
        // Try to find PID by process name (simplified - would need process enumeration)
        // For now, delegate to sample_by_pid with current process
        return sample_by_pid(get_current_pid());
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
        std::ifstream stat_file(proc_path + "/stat");
        if (stat_file.is_open()) {
            std::string line;
            std::getline(stat_file, line);
            std::istringstream iss(line);
            std::vector<std::string> tokens;
            std::string token;
            while (iss >> token) {
                tokens.push_back(token);
            }
            if (tokens.size() >= 15) {
                uint64_t utime = std::stoull(tokens[13]);
                uint64_t stime = std::stoull(tokens[14]);
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
        
        // System network and disk would require WMI
        usage.net_in_kbps = 0;
        usage.net_out_kbps = 0;
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
            
            // Calculate rate (simplified - would need previous values for accurate rate)
            usage.net_in_kbps = 0;  // Placeholder
            usage.net_out_kbps = 0; // Placeholder
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

private:
    int get_current_pid() const {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return getpid();
#endif
    }
    
#ifdef _WIN32
    struct ProcessCpuTime {
        std::chrono::steady_clock::time_point last_time;
        uint64_t last_total;
    };
    mutable std::map<int, ProcessCpuTime> process_cpu_times_;
#else
    mutable std::map<int, std::pair<int64_t, uint64_t>> prev_cpu_times_;
    mutable uint64_t prev_system_cpu_time_{0};
    mutable uint64_t prev_system_idle_time_{0};
    mutable int64_t prev_system_time_{0};
#endif
};

std::unique_ptr<ResourceMonitor> create_resource_monitor() {
    return std::make_unique<ResourceMonitorImpl>();
}

}
