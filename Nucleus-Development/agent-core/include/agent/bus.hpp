#pragma once

#include <string>
#include <functional>
#include <memory>
#include <cstdint>

namespace agent {

struct Envelope {
    std::string topic;          // e.g. ext.ps.exec.req
    std::string correlation_id; // GUID
    std::string payload_json;   // schema JSON
    int64_t ts_ms{0};
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
// pub_port: Port for PUB/SUB (publish/subscribe), default 5555
// req_port: Port for REQ/REP (request/reply), default 5556
std::unique_ptr<Bus> create_zmq_bus(Logger* logger = nullptr, int pub_port = 5555, int req_port = 5556);

}
