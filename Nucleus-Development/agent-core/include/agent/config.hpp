#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <vector>

namespace agent {

struct Config {
    struct Backend {
        std::string base_url{"https://api.nucleus.example.tbd"};
        std::string auth_path{"/deviceservices/api/Authentication/devicecertificatevalid/"};
        std::string is_registered_path{"/deviceservices/api/devicemanagement/isdeviceregistered/"};
        std::string get_activation_path{"/deviceservices/api/devicemanagement/getactivationinformation/"};
    } backend;

    struct Identity {
        bool is_gateway{false};
        std::string device_serial;
        std::string gateway_id;
        std::string uuid;
    } identity;

    struct TunnelInfo {
        bool enabled{false};
    } tunnel;

    struct Mqtt {
        std::string host{"mqtt.example.tbd"};
        int port{8883};
        int keepalive_s{30};
    } mqtt;

    struct Cert {
        std::string store_hint{"OS"};
        int renew_days{30};
        std::string cert_path;
    } cert;

    struct Retry {
        int max_attempts{5};
        int base_ms{500};
        int max_ms{8000};
    } retry;

    struct Resource {
        int cpu_max_pct{60};
        int mem_max_mb{512};
        int net_max_kbps{256};
        
        // Policy thresholds (percentages of max limits)
        double warn_threshold_pct{80.0};      // Warn at 80% of max
        double throttle_threshold_pct{90.0};   // Throttle at 90% of max
        double stop_threshold_pct{100.0};      // Stop at 100% of max
        
        // Critical extensions (whitelist - never stopped)
        std::vector<std::string> critical_extensions;
        
        // Enforcement interval (seconds)
        int enforcement_interval_s{10};
    } resource;

    struct Logging {
        std::string level{"info"};
        bool json{true};
        struct Throttle {
            bool enabled{true};
            int error_threshold{10};
            int window_seconds{60};
        } throttle;
    } logging;

    struct Ssm {
        std::string agent_path;
    } ssm;
    
    struct Service {
        int max_restart_attempts{5};
        int restart_base_delay_ms{1000};
        int restart_max_delay_ms{300000};  // 5 minutes
        double restart_jitter_factor{0.2};  // 20% jitter
        int quarantine_duration_s{3600};    // 1 hour
    } service;

    struct ZeroMQ {
        int pub_port{5555};  // Port for PUB/SUB (publish/subscribe)
        int req_port{5556};  // Port for REQ/REP (request/reply)
        bool curve_enabled{false};  // Enable CURVE encryption for inter-process (TCP) connections
        std::string curve_server_key;  // Server public key (40 chars base64)
        std::string curve_public_key;  // Client public key (40 chars base64)
        std::string curve_secret_key;  // Client secret key (40 chars base64)
    } zmq;

    struct Extensions {
        std::string manifest_path{"manifests/extensions.json"};
        int max_restart_attempts{3};
        int restart_base_delay_ms{1000};
        int restart_max_delay_ms{60000};    // 1 minute
        int quarantine_duration_s{300};     // 5 minutes
        int health_check_interval_s{30};
        int crash_detection_interval_s{5};
    } extensions;
    
    struct Telemetry {
        bool enabled{true};
        int sampling_interval_s{30};        // How often to sample
        int batch_size{10};                  // Readings per batch
        int cache_max_batches{1000};          // Max cached batches
        std::string cache_dir;                // Cache directory path
        std::string modality{"CS"};           // Modality for MQTT topic (default: CS for gateway)
        struct Alerts {
            double cpu_warn_pct{80.0};
            double cpu_critical_pct{95.0};
            int64_t mem_warn_mb{400};
            int64_t mem_critical_mb{480};
            int64_t net_warn_kbps{200};
            int64_t net_critical_kbps{240};
        } alerts;
    } telemetry;
};

std::unique_ptr<Config> load_config(const std::string& path);

} 
