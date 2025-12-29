#include "agent/telemetry_cache.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdlib>
#include "agent/uuid.hpp"

namespace fs = std::filesystem;

namespace agent {

TelemetryCache::TelemetryCache(const Config& config,
                               MqttClient* mqtt_client,
                               RetryPolicy* retry_policy,
                               Logger* logger,
                               Metrics* metrics,
                               const Identity& identity)
    : config_(config),
      mqtt_client_(mqtt_client),
      retry_policy_(retry_policy),
      logger_(logger),
      metrics_(metrics),
      identity_(identity) {
    
    // Determine cache directory
    if (!config.telemetry.cache_dir.empty()) {
        cache_dir_ = config.telemetry.cache_dir;
    } else {
        // Default to platform-specific cache directory
#ifdef _WIN32
        // Windows: Use %LOCALAPPDATA% or current directory
        const char* local_appdata = std::getenv("LOCALAPPDATA");
        if (local_appdata) {
            cache_dir_ = std::string(local_appdata) + "\\agent-core\\telemetry_cache";
        } else {
            cache_dir_ = ".\\telemetry_cache";
        }
#else
        // Linux: Use /var/lib/agent-core/telemetry_cache
        cache_dir_ = "/var/lib/agent-core/telemetry_cache";
#endif
    }
    
    // Create cache directory if it doesn't exist
    try {
        if (!fs::exists(cache_dir_)) {
            fs::create_directories(cache_dir_);
        }
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->log(LogLevel::Error, "TelemetryCache",
                        "Failed to create cache directory: " + std::string(e.what()));
        }
    }
}

bool TelemetryCache::store(const std::string& json_payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check cache size limit
    auto cached_files = get_cached_files();
    if (cached_files.size() >= static_cast<size_t>(config_.telemetry.cache_max_batches)) {
        // Remove oldest file (FIFO eviction)
        if (!cached_files.empty()) {
            try {
                fs::remove(cached_files[0]);
                if (logger_) {
                    logger_->log(LogLevel::Warn, "TelemetryCache",
                                "Cache full, evicting oldest batch");
                }
                if (metrics_) {
                    metrics_->increment("telemetry.cache.evictions");
                }
            } catch (const std::exception& e) {
                if (logger_) {
                    logger_->log(LogLevel::Error, "TelemetryCache",
                                "Failed to evict cache file: " + std::string(e.what()));
                }
            }
        }
    }
    
    // Generate filename
    std::string filename = generate_cache_filename();
    std::string filepath = (fs::path(cache_dir_) / filename).string();
    
    // Write to file
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            if (logger_) {
                logger_->log(LogLevel::Error, "TelemetryCache",
                            "Failed to open cache file for writing: " + filepath);
            }
            return false;
        }
        
        file << json_payload;
        file.close();
        
        if (logger_) {
            logger_->log(LogLevel::Debug, "TelemetryCache",
                        "Stored batch to cache: " + filename);
        }
        if (metrics_) {
            metrics_->increment("telemetry.cache.stored");
        }
        
        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->log(LogLevel::Error, "TelemetryCache",
                        "Failed to write cache file: " + std::string(e.what()));
        }
        return false;
    }
}

void TelemetryCache::retry_cached() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto cached_files = get_cached_files();
    if (cached_files.empty()) {
        return;
    }
    
    if (logger_) {
        logger_->log(LogLevel::Debug, "TelemetryCache",
                    "Retrying " + std::to_string(cached_files.size()) + " cached batches");
    }
    
    for (const auto& file_path : cached_files) {
        if (publish_batch(file_path)) {
            // Successfully published, file will be deleted in publish_batch
            if (metrics_) {
                metrics_->increment("telemetry.cache.retry_success");
            }
        } else {
            if (metrics_) {
                metrics_->increment("telemetry.cache.retry_failed");
            }
        }
    }
}

size_t TelemetryCache::get_cache_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return get_cached_files().size();
}

void TelemetryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto cached_files = get_cached_files();
    for (const auto& file_path : cached_files) {
        try {
            fs::remove(file_path);
        } catch (const std::exception& e) {
            if (logger_) {
                logger_->log(LogLevel::Error, "TelemetryCache",
                            "Failed to remove cache file: " + std::string(e.what()));
            }
        }
    }
    
    if (logger_) {
        logger_->log(LogLevel::Info, "TelemetryCache", "Cleared all cached batches");
    }
}

std::string TelemetryCache::build_mqtt_topic() const {
    std::string modality = config_.telemetry.modality.empty() ? "CS" : config_.telemetry.modality;
    std::string material_number = identity_.material_number.empty() ? 
        (identity_.is_gateway ? "GATEWAY" : "DEVICE") : identity_.material_number;
    std::string serial_number = identity_.serial_number.empty() ?
        identity_.device_serial : identity_.serial_number;
    
    return "/DeviceMonitoring/" + modality + "/" + material_number + "/" + serial_number;
}

std::vector<std::string> TelemetryCache::get_cached_files() const {
    std::vector<std::string> files;
    
    try {
        if (!fs::exists(cache_dir_)) {
            return files;
        }
        
        for (const auto& entry : fs::directory_iterator(cache_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path().string());
            }
        }
        
        // Sort by filename (which includes timestamp) for FIFO order
        std::sort(files.begin(), files.end());
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->log(LogLevel::Error, "TelemetryCache",
                        "Failed to list cache files: " + std::string(e.what()));
        }
    }
    
    return files;
}

bool TelemetryCache::publish_batch(const std::string& file_path) {
    // Read file content
    std::ifstream file(file_path);
    if (!file.is_open()) {
        if (logger_) {
            logger_->log(LogLevel::Error, "TelemetryCache",
                        "Failed to open cache file for reading: " + file_path);
        }
        return false;
    }
    
    std::string json_payload((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();
    
    // Attempt to publish using retry policy
    bool success = false;
    if (retry_policy_) {
        success = retry_policy_->execute([this, &json_payload]() {
            MqttMsg msg;
            msg.topic = build_mqtt_topic();
            msg.payload = json_payload;
            msg.qos = 1;  // At least once delivery
            
            try {
                mqtt_client_->publish(msg);
                return true;
            } catch (const std::exception& e) {
                if (logger_) {
                    logger_->log(LogLevel::Debug, "TelemetryCache",
                                "Publish failed: " + std::string(e.what()));
                }
                return false;
            }
        });
    } else {
        // No retry policy, try once
        MqttMsg msg;
        msg.topic = build_mqtt_topic();
        msg.payload = json_payload;
        msg.qos = 1;
        try {
            mqtt_client_->publish(msg);
            success = true;
        } catch (...) {
            success = false;
        }
    }
    
    if (success) {
        // Delete file on success
        try {
            fs::remove(file_path);
            if (logger_) {
                logger_->log(LogLevel::Debug, "TelemetryCache",
                            "Successfully published and removed: " + 
                            fs::path(file_path).filename().string());
            }
            if (metrics_) {
                metrics_->increment("telemetry.cache.published");
            }
        } catch (const std::exception& e) {
            if (logger_) {
                logger_->log(LogLevel::Warn, "TelemetryCache",
                            "Published but failed to remove cache file: " + 
                            std::string(e.what()));
            }
        }
    }
    
    return success;
}

std::string TelemetryCache::generate_cache_filename() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << "batch_";
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    oss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    oss << "_" << util::generate_uuid();
    oss << ".json";
    
    return oss.str();
}

}

