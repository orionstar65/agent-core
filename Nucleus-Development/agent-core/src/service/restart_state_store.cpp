#include "agent/restart_state_store.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <errno.h>
#else
#include <unistd.h>
#include <errno.h>
#endif

using json = nlohmann::json;

namespace agent {

class RestartStateStoreImpl : public RestartStateStore {
public:
    explicit RestartStateStoreImpl(const std::string& state_file_path)
        : state_file_path_(state_file_path) {}
    
    bool ensure_parent_directory() const {
        size_t last_sep = state_file_path_.find_last_of("/\\");
        if (last_sep == std::string::npos) {
            return true;  // No parent directory needed
        }
        
        std::string parent_dir = state_file_path_.substr(0, last_sep);
        if (parent_dir.empty()) {
            return true;
        }
        
#ifdef _WIN32
        if (_mkdir(parent_dir.c_str()) != 0 && errno != EEXIST) {
            // Try creating parent directories recursively
            // Build up the path incrementally
            size_t pos = 0;
            // Start from position 0 to find all separators correctly
            while ((pos = parent_dir.find_first_of("/\\", pos)) != std::string::npos) {
                // Extract directory up to but not including the separator
                std::string subdir = parent_dir.substr(0, pos);
                // Skip invalid directory names:
                // 1. Single drive letter without colon (e.g., "C")
                // 2. Drive letter with colon (e.g., "C:") - drive root already exists
                // Only create directories that are actual path components
                bool is_single_drive_letter = (subdir.length() == 1 && 
                    ((subdir[0] >= 'A' && subdir[0] <= 'Z') || 
                     (subdir[0] >= 'a' && subdir[0] <= 'z')));
                bool is_drive_root = (subdir.length() == 2 && subdir[1] == ':');
                
                if (!subdir.empty() && !is_single_drive_letter && !is_drive_root) {
                    if (_mkdir(subdir.c_str()) != 0 && errno != EEXIST) {
                        // Continue trying - some directories may already exist
                    }
                }
                pos++; // Move past the separator for next iteration
            }
            // Try creating the final directory again
            if (_mkdir(parent_dir.c_str()) != 0 && errno != EEXIST) {
                return false;
            }
        }
#else
        if (mkdir(parent_dir.c_str(), 0755) != 0 && errno != EEXIST) {
            // Try creating parent directories recursively
            // Build up the path incrementally
            size_t pos = 0;
            while ((pos = parent_dir.find_first_of("/", pos + 1)) != std::string::npos) {
                // Extract directory up to but not including the separator
                // For "/var/lib", this creates "/var" first, then "/var/lib"
                std::string subdir = parent_dir.substr(0, pos);
                if (!subdir.empty() && mkdir(subdir.c_str(), 0755) != 0 && errno != EEXIST) {
                    // Continue trying - some directories may already exist
                }
            }
            // Try creating the final directory again
            if (mkdir(parent_dir.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
#endif
        return true;
    }
    
    bool save(const PersistedRestartState& state) override {
        try {
            // Ensure parent directory exists
            if (!ensure_parent_directory()) {
                std::cerr << "RestartStateStore: Failed to create parent directory\n";
                return false;
            }
            
            json j;
            j["restart_count"] = state.restart_count;
            j["last_restart_timestamp"] = state.last_restart_timestamp;
            j["quarantine_start_timestamp"] = state.quarantine_start_timestamp;
            j["in_quarantine"] = state.in_quarantine;
            
            std::ofstream file(state_file_path_);
            if (!file) {
                std::cerr << "RestartStateStore: Failed to open file: " << state_file_path_ << "\n";
                return false;
            }
            
            file << j.dump(2);
            return file.good();
        } catch (const std::exception& e) {
            std::cerr << "RestartStateStore: Failed to save state: " << e.what() << "\n";
            return false;
        }
    }
    
    bool load(PersistedRestartState& state) override {
        try {
            std::ifstream file(state_file_path_);
            if (!file) {
                return false;
            }
            
            json j;
            file >> j;
            
            state.restart_count = j.value("restart_count", 0);
            state.last_restart_timestamp = j.value("last_restart_timestamp", 0LL);
            state.quarantine_start_timestamp = j.value("quarantine_start_timestamp", 0LL);
            state.in_quarantine = j.value("in_quarantine", false);
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "RestartStateStore: Failed to load state: " << e.what() << "\n";
            return false;
        }
    }
    
    bool exists() const override {
        struct stat buffer;
        return (stat(state_file_path_.c_str(), &buffer) == 0);
    }
    
    bool clear() override {
        if (exists()) {
            return (std::remove(state_file_path_.c_str()) == 0);
        }
        return true;
    }

private:
    std::string state_file_path_;
};

std::unique_ptr<RestartStateStore> create_restart_state_store(const std::string& state_file_path) {
    return std::make_unique<RestartStateStoreImpl>(state_file_path);
}

}
