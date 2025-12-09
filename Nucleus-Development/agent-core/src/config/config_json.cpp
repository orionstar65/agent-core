#include "agent/config.hpp"
#include "agent/path_utils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <filesystem>

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
                std::string cert_path = cert["certPath"].get<std::string>();
                
                // Resolve cert path relative to executable directory
                // This ensures relative paths like ./cert_base64(200000).txt work correctly
                // when running as Windows service (which runs from C:\Windows\System32)
                if (!cert_path.empty()) {
                    std::filesystem::path cert_path_obj(cert_path);
                    
                    // Check if path is absolute (Windows: starts with drive letter or \\, Linux: starts with /)
                    bool is_absolute = cert_path_obj.is_absolute();
                    
                    if (!is_absolute) {
                        // Get executable directory
                        std::string exe_dir = util::get_executable_directory();
                        
                        if (!exe_dir.empty()) {
                            // Combine executable directory with cert path
                            // Since exe_dir is absolute, the result will be absolute
                            std::filesystem::path cert_full_path = std::filesystem::path(exe_dir) / cert_path;
                            
                            // Normalize the path (resolves .. and . components)
                            cert_full_path = cert_full_path.lexically_normal();
                            
                            // Convert to absolute path to ensure it's fully resolved
                            try {
                                std::filesystem::path abs_cert_path = std::filesystem::absolute(cert_full_path);
                                config->cert.cert_path = abs_cert_path.lexically_normal().string();
                            } catch (...) {
                                // If absolute() fails, use the normalized path as-is
                                config->cert.cert_path = cert_full_path.string();
                            }
                        } else {
                            // If we can't get executable directory, fall back to resolving relative to current working directory
                            std::string resolved = util::resolve_path(cert_path);
                            if (!resolved.empty()) {
                                config->cert.cert_path = resolved;
                            } else {
                                config->cert.cert_path = cert_path;
                            }
                        }
                    } else {
                        // Path is already absolute, use resolve_path to get canonical form
                        std::string resolved = util::resolve_path(cert_path);
                        if (!resolved.empty()) {
                            config->cert.cert_path = resolved;
                        } else {
                            // If resolve_path fails, normalize and use as-is
                            try {
                                std::filesystem::path norm_path = std::filesystem::path(cert_path).lexically_normal();
                                config->cert.cert_path = norm_path.string();
                            } catch (...) {
                                config->cert.cert_path = cert_path;
                            }
                        }
                    }
                } else {
                    config->cert.cert_path = cert_path;
                }
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
        }
        
        // Parse SSM
        if (j.contains("ssm")) {
            auto& ssm = j["ssm"];
            if (ssm.contains("agentPath")) {
                config->ssm.agent_path = ssm["agentPath"].get<std::string>();
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
        }
        
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
