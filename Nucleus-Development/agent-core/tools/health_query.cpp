#include "agent/bus.hpp"
#include "agent/config.hpp"
#include "agent/telemetry.hpp"
#include "agent/uuid.hpp"
#include <iostream>
#include <chrono>
#include <mutex>
#include <condition_variable>

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
        
        // Subscribe to reply topic and wait for response
        Envelope reply;
        bool reply_received = false;
        std::mutex reply_mutex;
        std::condition_variable reply_cv;
        
        bus->subscribe("agent.health.query.reply", 
            [&reply, &reply_received, &reply_mutex, &reply_cv, req_correlation_id = req.correlation_id]
            (const Envelope& msg) {
                if (msg.correlation_id == req_correlation_id) {
                    std::lock_guard<std::mutex> lock(reply_mutex);
                    reply = msg;
                    reply_received = true;
                    reply_cv.notify_one();
                }
            });
        
        // Publish the request
        bus->publish(req);
        
        // Wait for reply (with timeout)
        std::unique_lock<std::mutex> lock(reply_mutex);
        if (!reply_cv.wait_for(lock, std::chrono::seconds(5), [&reply_received] { return reply_received; })) {
            std::cerr << "Error: Timeout waiting for health query response\n";
            return 1;
        }
        
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
