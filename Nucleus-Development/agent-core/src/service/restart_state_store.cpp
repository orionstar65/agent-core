#include "agent/restart_state_store.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace agent {

class RestartStateStoreImpl : public RestartStateStore {
public:
    explicit RestartStateStoreImpl(const std::string& state_file_path)
        : state_file_path_(state_file_path) {}
    
    bool save(const PersistedRestartState& state) override {
        try {
            json j;
            j["restart_count"] = state.restart_count;
            j["last_restart_timestamp"] = state.last_restart_timestamp;
            j["quarantine_start_timestamp"] = state.quarantine_start_timestamp;
            j["in_quarantine"] = state.in_quarantine;
            
            std::ofstream file(state_file_path_);
            if (!file) {
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
