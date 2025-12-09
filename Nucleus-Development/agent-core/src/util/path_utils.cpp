#include "agent/path_utils.hpp"
#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <vector>
#else
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#endif

namespace agent {
namespace util {

std::string resolve_path(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    
#ifdef _WIN32
    // Windows: Use GetFullPathNameA to resolve the path
    std::vector<char> buffer(MAX_PATH);
    DWORD length = GetFullPathNameA(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    
    if (length == 0) {
        // Error getting full path
        return "";
    }
    
    // If buffer was too small, resize and try again
    if (length > buffer.size()) {
        buffer.resize(length);
        length = GetFullPathNameA(path.c_str(), length, buffer.data(), nullptr);
        if (length == 0 || length > buffer.size()) {
            return "";
        }
    }
    
    return std::string(buffer.data(), length);
#else
    // Linux: Use realpath to resolve the path
    char resolved_path[PATH_MAX];
    if (realpath(path.c_str(), resolved_path) == nullptr) {
        return "";
    }
    return std::string(resolved_path);
#endif
}

std::string get_executable_directory() {
#ifdef _WIN32
    // Windows: Use GetModuleFileName to get executable path
    std::vector<char> buffer(MAX_PATH);
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    
    if (length == 0) {
        return "";
    }
    
    // If buffer was too small, resize and try again
    if (length >= buffer.size()) {
        buffer.resize(length + 1);
        length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return "";
        }
    }
    
    // Get the full absolute path and normalize it
    std::filesystem::path exe_path(buffer.data());
    try {
        // Make sure it's absolute and normalized
        std::filesystem::path abs_exe_path = std::filesystem::absolute(exe_path);
        return abs_exe_path.parent_path().lexically_normal().string();
    } catch (...) {
        // Fallback to just parent_path if absolute() fails
        return exe_path.parent_path().string();
    }
#else
    // Linux: Use readlink on /proc/self/exe
    char exe_path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    
    if (length == -1) {
        return "";
    }
    
    exe_path[length] = '\0';
    std::filesystem::path exe_file_path(exe_path);
    return exe_file_path.parent_path().string();
#endif
}

}  // namespace util
}  // namespace agent

