#pragma once

#include <string>
#include <chrono>
#include <memory>

namespace agent {

struct PersistedRestartState {
    int restart_count{0};
    long long last_restart_timestamp{0};
    long long quarantine_start_timestamp{0};
    bool in_quarantine{false};
};

class RestartStateStore {
public:
    virtual ~RestartStateStore() = default;
    
    virtual bool save(const PersistedRestartState& state) = 0;
    virtual bool load(PersistedRestartState& state) = 0;
    virtual bool exists() const = 0;
    virtual bool clear() = 0;
};

std::unique_ptr<RestartStateStore> create_restart_state_store(const std::string& state_file_path);

}
