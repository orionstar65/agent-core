#pragma once

#include <string>
#include <memory>

namespace agent {

struct Config {
    struct Backend {
        std::string base_url{"https://api.nucleus.example.tbd"};
    } backend;

    struct Identity {
        bool is_gateway{false};
        std::string device_serial;
        std::string gateway_id;
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
    } logging;
};

std::unique_ptr<Config> load_config(const std::string& path);

} 
