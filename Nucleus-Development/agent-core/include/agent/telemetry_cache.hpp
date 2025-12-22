#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "config.hpp"
#include "retry.hpp"
#include "mqtt_client.hpp"
#include "identity.hpp"
#include "telemetry.hpp"

namespace agent {

class TelemetryCache {
public:
    TelemetryCache(const Config& config,
                   MqttClient* mqtt_client,
                   RetryPolicy* retry_policy,
                   Logger* logger,
                   Metrics* metrics,
                   const Identity& identity);
    
    // Store a batch for later retry
    bool store(const std::string& json_payload);
    
    // Attempt to publish all cached batches
    void retry_cached();
    
    // Get count of cached batches
    size_t get_cache_size() const;
    
    // Clear all cached batches
    void clear();

private:
    const Config& config_;
    MqttClient* mqtt_client_;
    RetryPolicy* retry_policy_;
    Logger* logger_;
    Metrics* metrics_;
    const Identity& identity_;
    
    mutable std::mutex mutex_;
    std::string cache_dir_;
    
    std::string build_mqtt_topic() const;
    std::vector<std::string> get_cached_files() const;
    bool publish_batch(const std::string& file_path);
    std::string generate_cache_filename() const;
};

}

