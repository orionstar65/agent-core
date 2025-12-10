#pragma once

#include <string>
#include <functional>
#include <memory>
#include <cstdint>
#include <map>
#include "identity.hpp"
#include "auth_manager.hpp"
#include "config.hpp"

namespace agent {

struct AuthContext {
    std::string device_serial;
    std::string gateway_id;      // empty if not a gateway
    std::string uuid;
    bool cert_valid{false};
    int64_t cert_expires_ms{0};  // 0 if not set
};

struct Envelope {
    std::string topic;          // e.g. ext.ps.exec.req
    std::string correlation_id; // GUID
    std::string payload_json;   // schema JSON
    int64_t ts_ms{0};
    std::map<std::string, std::string> headers;  // key-value metadata
    AuthContext auth_context;   // authentication context
};

class Logger;

class Bus {
public:
    virtual ~Bus() = default;
    
    // Publish message (PUB/SUB pattern)
    virtual void publish(const Envelope& envelope) = 0;
    
    // Send request and wait for reply (REQ/REP pattern)
    virtual void request(const Envelope& req, Envelope& reply) = 0;
    
    // Subscribe to topic with callback
    virtual void subscribe(const std::string& topic,
                          std::function<void(const Envelope&)> callback) = 0;
};

// Create ZeroMQ-based bus implementation
// logger: Optional logger for bus operations
// zmq_config: ZeroMQ configuration (ports, CURVE encryption settings)
std::unique_ptr<Bus> create_zmq_bus(Logger* logger, const Config::ZeroMQ& zmq_config);

// Helper function to populate auth_context from Identity and CertState
// cert_state: Certificate state from AuthManager
// Returns populated AuthContext
AuthContext create_auth_context(const Identity& identity, CertState cert_state, int64_t cert_expires_ms = 0);

}
