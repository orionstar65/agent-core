#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <zmq.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "agent/bus.hpp"
#include "agent/envelope_serialization.hpp"

using namespace agent;

std::atomic<bool> g_running{true};

std::string get_timestamp() {
    auto now = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

void log(const std::string& level, const std::string& message, const std::string& details = "") {
    std::cout << "[" << get_timestamp() << "] [" << level << "] " << message;
    if (!details.empty()) {
        std::cout << " " << details;
    }
    std::cout << std::endl;  // Use endl to flush
}

void signal_handler(int signum) {
    log("INFO", "Sample Extension: Received signal", std::to_string(signum));
    g_running = false;
}

int main(int argc, char* argv[]) {
    log("INFO", "=== Sample Extension v0.1.0 ===");
    log("INFO", "Sample Extension: Starting");
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    for (int i = 1; i < argc; i++) {
        log("DEBUG", "Arg[" + std::to_string(i) + "]:", argv[i]);
    }
    
    zmq::context_t context(1);
    
    // REP socket for request/reply pattern
    // Extensions bind REP socket, agent-core connects with REQ socket
    zmq::socket_t rep_socket(context, ZMQ_REP);
#ifdef _WIN32
    std::string rep_endpoint = "tcp://127.0.0.1:5556";  // Windows uses TCP
#else
    std::string rep_endpoint = "ipc:///run/agent-core/bus-req";  // Linux uses IPC
#endif
    try {
        rep_socket.bind(rep_endpoint);
    } catch (const zmq::error_t& e) {
        log("ERROR", "Sample Extension: Failed to bind REP socket", "(" + rep_endpoint + "): " + std::to_string(e.num()));
        return 1;
    }
    
    int rep_timeout = 100;  // Short timeout for non-blocking checks
    rep_socket.set(zmq::sockopt::rcvtimeo, rep_timeout);
    
    log("INFO", "Sample Extension: Bound REP socket", "(" + rep_endpoint + ")");
    
    // SUB socket for pub/sub pattern (to receive MQTT messages)
    zmq::socket_t sub_socket(context, ZMQ_SUB);
#ifdef _WIN32
    std::string sub_endpoint = "tcp://127.0.0.1:5555";  // Windows uses TCP
#else
    std::string sub_endpoint = "ipc:///run/agent-core/bus-pub";  // Linux uses IPC
#endif
    try {
        sub_socket.connect(sub_endpoint);
        // Subscribe to mqtt.status_request topic
        sub_socket.set(zmq::sockopt::subscribe, "mqtt.status_request");
    } catch (const zmq::error_t& e) {
        log("ERROR", "Sample Extension: Failed to connect SUB socket", "(" + sub_endpoint + "): " + std::to_string(e.num()));
        return 1;
    }
    
    sub_socket.set(zmq::sockopt::rcvtimeo, 100);  // Short timeout for non-blocking
    
    log("INFO", "Sample Extension: Connected SUB socket", "(" + sub_endpoint + ")");
    log("INFO", "Sample Extension: Subscribed to topic: mqtt.status_request");
    
    int request_count = 0;
    int mqtt_message_count = 0;
    
    while (g_running) {
        // Check for MQTT messages on SUB socket (PUB/SUB pattern)
        // ZeroMQ PUB/SUB uses multipart: [topic][payload]
        zmq::message_t topic_msg;
        if (sub_socket.recv(topic_msg, zmq::recv_flags::dontwait)) {
            // Received topic part, now get payload part
            zmq::message_t payload_msg;
            if (sub_socket.recv(payload_msg, zmq::recv_flags::dontwait)) {
                std::string topic_str(static_cast<const char*>(topic_msg.data()), topic_msg.size());
                std::string mqtt_json(static_cast<const char*>(payload_msg.data()), payload_msg.size());
                
                log("DEBUG", "Received ZeroMQ PUB/SUB message", "topic=" + topic_str + ", len=" + std::to_string(mqtt_json.length()));
                
                Envelope mqtt_env;
                if (deserialize_envelope(mqtt_json, mqtt_env)) {
                    mqtt_message_count++;
                    log("INFO", "=== MQTT Message #" + std::to_string(mqtt_message_count) + " ===");
                    log("INFO", "  Topic:", mqtt_env.topic);
                    log("INFO", "  Payload:", mqtt_env.payload_json);
                    log("INFO", "  Timestamp:", std::to_string(mqtt_env.ts_ms));
                    
                    // Print to console as per requirements
                    std::cout << "\n*** STATUS_REQUEST received via ZeroMQ ***\n";
                    std::cout << "    Payload: " << mqtt_env.payload_json << "\n";
                    std::cout << std::endl;  // Flush output
                } else {
                    log("ERROR", "Failed to deserialize MQTT message", mqtt_json);
                }
            } else {
                log("WARN", "Received topic but failed to get payload");
            }
        }
        
        // Check for REQ/REP messages
        zmq::message_t request_msg;
        if (rep_socket.recv(request_msg, zmq::recv_flags::dontwait)) {
            std::string request_json(static_cast<const char*>(request_msg.data()), request_msg.size());
            Envelope req;
            if (!deserialize_envelope(request_json, req)) {
                log("ERROR", "Sample Extension: Failed to deserialize request");
                // Still need to send a reply to maintain REQ/REP state
                Envelope error_reply;
                error_reply.topic = "error";
                error_reply.payload_json = R"({"error":"deserialization failed"})";
                std::string error_json = serialize_envelope(error_reply);
                zmq::message_t error_msg(error_json.data(), error_json.size());
                rep_socket.send(error_msg, zmq::send_flags::none);
                continue;
            }
            
            request_count++;
            log("INFO", "=== REQ/REP Request #" + std::to_string(request_count) + " ===");
            log("INFO", "  Topic:", req.topic);
            log("INFO", "  Correlation ID:", req.correlation_id);
            log("INFO", "  Payload:", req.payload_json);
            
            // Build reply
            Envelope reply;
            reply.topic = req.topic + ".reply";
            reply.correlation_id = req.correlation_id;  // Preserve correlation ID
            reply.payload_json = R"({"status":"ok","message":"echo reply"})";
            reply.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::string reply_json = serialize_envelope(reply);
            zmq::message_t reply_msg(reply_json.data(), reply_json.size());
            rep_socket.send(reply_msg, zmq::send_flags::none);
            
            log("INFO", "  Sent reply with correlation ID:", reply.correlation_id);
        }
        
        // Short sleep to avoid busy loop but stay responsive
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    log("INFO", "Sample Extension: Shutting down");
    return 0;
}
