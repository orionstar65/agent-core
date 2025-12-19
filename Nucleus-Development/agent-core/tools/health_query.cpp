#include "agent/bus.hpp"
#include "agent/config.hpp"
#include "agent/telemetry.hpp"
#include "agent/uuid.hpp"
#include <iostream>
#include <chrono>

using namespace agent;

int main(int, char**) {
    std::cout << "=== Agent Core Health Query Tool ===\n\n";
    
    try {
        // Create logger and ZeroMQ config
        auto logger = create_logger("warn", false);
        Config::ZeroMQ zmq_config;
        zmq_config.pub_port = 5555;
        zmq_config.req_port = 5556;
        
        // Create ZeroMQ bus
        auto bus = create_zmq_bus(logger.get(), zmq_config);
        
        // Build health query request
        Envelope req;
        req.topic = "agent.health.query";
        req.correlation_id = util::generate_uuid();
        req.payload_json = "{}";
        req.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::cout << "Sending health query...\n";
        std::cout << "  Topic: " << req.topic << "\n";
        std::cout << "  Correlation ID: " << req.correlation_id << "\n\n";
        
        // Send request and wait for reply
        Envelope reply;
        bus->request(req, reply);
        
        std::cout << "Received health response:\n";
        std::cout << "  Topic: " << reply.topic << "\n";
        std::cout << "  Correlation ID: " << reply.correlation_id << "\n";
        std::cout << "  Timestamp: " << reply.ts_ms << "\n\n";
        
        std::cout << "Health Status:\n";
        std::cout << reply.payload_json << "\n\n";
        
        // Parse and pretty print (simple version)
        std::cout << "=================================\n";
        std::cout << "Query successful!\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
