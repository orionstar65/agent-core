#pragma once

#include <string>
#include <memory>

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
};

std::unique_ptr<Config> load_config(const std::string& path);

} 
