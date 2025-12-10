#pragma once

#include "agent/bus.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <exception>

namespace agent {
namespace envelope_json {

using json = nlohmann::json;

template<typename EnvType>
inline std::string serialize_envelope_template(const EnvType& envelope, int version = 2) {
    try {
        json j;
        j["v"] = version;
        j["topic"] = envelope.topic;
        j["correlationId"] = envelope.correlation_id;
        
        // Parse payload_json as JSON (it's already JSON, but we need to parse it to embed it properly)
        try {
            j["payload"] = json::parse(envelope.payload_json);
        } catch (const json::parse_error&) {
            // If payload_json is not valid JSON, treat it as a string
            j["payload"] = envelope.payload_json;
            }
        
        j["ts"] = envelope.ts_ms;
        
        // Version 2 adds headers and auth_context
        if (version >= 2) {
            // Serialize headers
            if (!envelope.headers.empty()) {
                json headers_obj = json::object();
                for (const auto& pair : envelope.headers) {
                    headers_obj[pair.first] = pair.second;
            }
                j["headers"] = headers_obj;
}

            // Serialize auth_context
            json auth_obj;
            auth_obj["deviceSerial"] = envelope.auth_context.device_serial;
            auth_obj["gatewayId"] = envelope.auth_context.gateway_id;
            auth_obj["uuid"] = envelope.auth_context.uuid;
            auth_obj["certValid"] = envelope.auth_context.cert_valid;
            if (envelope.auth_context.cert_expires_ms > 0) {
                auth_obj["certExpiresMs"] = envelope.auth_context.cert_expires_ms;
    }
            j["authContext"] = auth_obj;
    }
        
        return j.dump();  // Compact JSON (no whitespace)
    } catch (const std::exception&) {
        return "{}";  // Return empty JSON on error
    }
}

template<typename EnvType>
inline bool deserialize_envelope_template(const std::string& json_str, EnvType& envelope) {
    try {
        json j = json::parse(json_str);
        
        // Extract version
        int version = j.value("v", 1);
        if (version < 1 || version > 2) {
            // Reject unsupported versions (future versions > 2)
            if (version > 2) {
                return false;
            }
            // version < 1 is invalid
            if (version < 1) {
                return false;
            }
        }
        
        // Extract required fields
        if (!j.contains("topic")) {
            return false;  // Topic is required
        }
        
        envelope.topic = j.value("topic", std::string());
        envelope.correlation_id = j.value("correlationId", std::string());
        
        // Extract payload - keep as JSON string
        if (j.contains("payload")) {
            if (j["payload"].is_string()) {
                envelope.payload_json = j["payload"].get<std::string>();
            } else {
                // Serialize JSON object/array back to string
                envelope.payload_json = j["payload"].dump();
            }
        } else {
            envelope.payload_json = "{}";
        }
        
        envelope.ts_ms = j.value("ts", int64_t(0));
        
        // Version 2 fields (optional for backward compatibility)
        if (version >= 2) {
            // Deserialize headers
            envelope.headers.clear();
            if (j.contains("headers") && j["headers"].is_object()) {
                for (auto& [key, value] : j["headers"].items()) {
                    if (value.is_string()) {
                        envelope.headers[key] = value.get<std::string>();
                    }
                }
            }
            
            // Deserialize auth_context
            if (j.contains("authContext") && j["authContext"].is_object()) {
                const json& auth_obj = j["authContext"];
                envelope.auth_context.device_serial = auth_obj.value("deviceSerial", std::string());
                envelope.auth_context.gateway_id = auth_obj.value("gatewayId", std::string());
                envelope.auth_context.uuid = auth_obj.value("uuid", std::string());
                envelope.auth_context.cert_valid = auth_obj.value("certValid", false);
                envelope.auth_context.cert_expires_ms = auth_obj.value("certExpiresMs", int64_t(0));
            } else {
                // Initialize with defaults if not present
                envelope.auth_context = AuthContext{};
            }
        } else {
            // Version 1: initialize with defaults
            envelope.headers.clear();
            envelope.auth_context = AuthContext{};
        }
        
        return true;
    } catch (const json::parse_error&) {
        return false;  // Invalid JSON
    } catch (const std::exception&) {
        return false;  // Other errors
    }
}

inline std::string serialize_envelope(const Envelope& envelope) {
    // Use version 2 by default (supports headers and auth_context)
    return serialize_envelope_template(envelope, 2);
}

inline bool deserialize_envelope(const std::string& json_str, Envelope& envelope) {
    return deserialize_envelope_template(json_str, envelope);
}

}
}
