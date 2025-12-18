#include "agent/config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>

using json = nlohmann::json;

namespace agent {

std::unique_ptr<Config> load_config(const std::string& path) {
    auto config = std::make_unique<Config>();
    
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open config file: " << path 
                  << ", using defaults\n";
        return config;
    }
    
    try {
        json j = json::parse(file);
        
        // Parse backend
        if (j.contains("backend")) {
            auto& backend = j["backend"];
            if (backend.contains("baseUrl")) {
                config->backend.base_url = backend["baseUrl"].get<std::string>();
            }
            if (backend.contains("authPath")) {
                config->backend.auth_path = backend["authPath"].get<std::string>();
            }
            if (backend.contains("isRegisteredPath")) {
                config->backend.is_registered_path = backend["isRegisteredPath"].get<std::string>();
            }
            if (backend.contains("getActivationPath")) {
                config->backend.get_activation_path = backend["getActivationPath"].get<std::string>();
            }
        }
        
        // Parse identity
        if (j.contains("identity")) {
            auto& identity = j["identity"];
            if (identity.contains("isGateway")) {
                config->identity.is_gateway = identity["isGateway"].get<bool>();
            }
            if (identity.contains("deviceSerial")) {
                config->identity.device_serial = identity["deviceSerial"].get<std::string>();
            }
            if (identity.contains("gatewayId")) {
                config->identity.gateway_id = identity["gatewayId"].get<std::string>();
            }
            if (identity.contains("uuid")) {
                config->identity.uuid = identity["uuid"].get<std::string>();
            }
        }
        
        // Parse tunnel
        if (j.contains("tunnelInfo") && j["tunnelInfo"].contains("enabled")) {
            config->tunnel.enabled = j["tunnelInfo"]["enabled"].get<bool>();
        }
        
        // Parse MQTT
        if (j.contains("mqtt")) {
            auto& mqtt = j["mqtt"];
            if (mqtt.contains("host")) {
                config->mqtt.host = mqtt["host"].get<std::string>();
            }
            if (mqtt.contains("port")) {
                config->mqtt.port = mqtt["port"].get<int>();
            }
            if (mqtt.contains("keepalive")) {
                config->mqtt.keepalive_s = mqtt["keepalive"].get<int>();
            }
        }
        
        // Parse cert
        if (j.contains("cert")) {
            auto& cert = j["cert"];
            if (cert.contains("storeHint")) {
                config->cert.store_hint = cert["storeHint"].get<std::string>();
            }
            if (cert.contains("certPath")) {
                config->cert.cert_path = cert["certPath"].get<std::string>();
            }
            if (cert.contains("renewDays")) {
                config->cert.renew_days = cert["renewDays"].get<int>();
            }
        }
        
        // Parse retry
        if (j.contains("retry")) {
            auto& retry = j["retry"];
            if (retry.contains("maxAttempts")) {
                config->retry.max_attempts = retry["maxAttempts"].get<int>();
            }
            if (retry.contains("baseMs")) {
                config->retry.base_ms = retry["baseMs"].get<int>();
            }
            if (retry.contains("maxMs")) {
                config->retry.max_ms = retry["maxMs"].get<int>();
            }
        }
        
        // Parse resource
        if (j.contains("resource")) {
            auto& resource = j["resource"];
            if (resource.contains("cpuMaxPct")) {
                config->resource.cpu_max_pct = resource["cpuMaxPct"].get<int>();
            }
            if (resource.contains("memMaxMB")) {
                config->resource.mem_max_mb = resource["memMaxMB"].get<int>();
            }
            if (resource.contains("netMaxKBps")) {
                config->resource.net_max_kbps = resource["netMaxKBps"].get<int>();
            }
        }
        
        // Parse logging
        if (j.contains("logging")) {
            auto& logging = j["logging"];
            if (logging.contains("level")) {
                config->logging.level = logging["level"].get<std::string>();
            }
            if (logging.contains("json")) {
                config->logging.json = logging["json"].get<bool>();
            }
            if (logging.contains("throttle")) {
                auto& throttle = logging["throttle"];
                if (throttle.contains("enabled")) {
                    config->logging.throttle.enabled = throttle["enabled"].get<bool>();
                }
                if (throttle.contains("errorThreshold")) {
                    config->logging.throttle.error_threshold = throttle["errorThreshold"].get<int>();
                }
                if (throttle.contains("windowSeconds")) {
                    config->logging.throttle.window_seconds = throttle["windowSeconds"].get<int>();
                }
            }
        }
        
        // Parse SSM
        if (j.contains("ssm")) {
            auto& ssm = j["ssm"];
            if (ssm.contains("agentPath")) {
                config->ssm.agent_path = ssm["agentPath"].get<std::string>();
            }
        }
        
        // Parse Service
        if (j.contains("service")) {
            auto& service = j["service"];
            if (service.contains("maxRestartAttempts")) {
                config->service.max_restart_attempts = service["maxRestartAttempts"].get<int>();
            }
            if (service.contains("restartBaseDelayMs")) {
                config->service.restart_base_delay_ms = service["restartBaseDelayMs"].get<int>();
            }
            if (service.contains("restartMaxDelayMs")) {
                config->service.restart_max_delay_ms = service["restartMaxDelayMs"].get<int>();
            }
            if (service.contains("restartJitterFactor")) {
                config->service.restart_jitter_factor = service["restartJitterFactor"].get<double>();
            }
            if (service.contains("quarantineDurationS")) {
                config->service.quarantine_duration_s = service["quarantineDurationS"].get<int>();
            }
        }
        
        // Parse ZeroMQ
        if (j.contains("zmq")) {
            auto& zmq = j["zmq"];
            if (zmq.contains("pubPort")) {
                config->zmq.pub_port = zmq["pubPort"].get<int>();
            }
            if (zmq.contains("reqPort")) {
                config->zmq.req_port = zmq["reqPort"].get<int>();
            }
            if (zmq.contains("curveEnabled")) {
                config->zmq.curve_enabled = zmq["curveEnabled"].get<bool>();
            }
            if (zmq.contains("curveServerKey")) {
                config->zmq.curve_server_key = zmq["curveServerKey"].get<std::string>();
            }
            if (zmq.contains("curvePublicKey")) {
                config->zmq.curve_public_key = zmq["curvePublicKey"].get<std::string>();
            }
            if (zmq.contains("curveSecretKey")) {
                config->zmq.curve_secret_key = zmq["curveSecretKey"].get<std::string>();
            }
        }
        
        // Parse Extensions
        if (j.contains("extensions")) {
            auto& ext = j["extensions"];
            if (ext.contains("manifestPath")) {
                config->extensions.manifest_path = ext["manifestPath"].get<std::string>();
            }
            if (ext.contains("maxRestartAttempts")) {
                config->extensions.max_restart_attempts = ext["maxRestartAttempts"].get<int>();
            }
            if (ext.contains("restartBaseDelayMs")) {
                config->extensions.restart_base_delay_ms = ext["restartBaseDelayMs"].get<int>();
            }
            if (ext.contains("restartMaxDelayMs")) {
                config->extensions.restart_max_delay_ms = ext["restartMaxDelayMs"].get<int>();
            }
            if (ext.contains("quarantineDurationS")) {
                config->extensions.quarantine_duration_s = ext["quarantineDurationS"].get<int>();
            }
            if (ext.contains("healthCheckIntervalS")) {
                config->extensions.health_check_interval_s = ext["healthCheckIntervalS"].get<int>();
            }
            if (ext.contains("crashDetectionIntervalS")) {
                config->extensions.crash_detection_interval_s = ext["crashDetectionIntervalS"].get<int>();
            }
        }
        
        return config;
        
        std::cout << "Config loaded successfully from: " << path << "\n";
        std::cout << "  Backend URL: " << config->backend.base_url << "\n";
        std::cout << "  Device Serial: " << config->identity.device_serial << "\n";
        std::cout << "  UUID: " << config->identity.uuid << "\n";
        std::cout << "  Cert Path: " << config->cert.cert_path << "\n";
        std::cout << "  Tunnel Enabled: " << (config->tunnel.enabled ? "true" : "false") << "\n";
        
    } catch (const json::exception& e) {
        std::cerr << "Error parsing JSON config: " << e.what() << "\n";
        throw std::runtime_error("Failed to parse config file");
    }
    
    return config;
}

}
