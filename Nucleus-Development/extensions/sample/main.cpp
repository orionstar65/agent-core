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
    std::cout << "\n";
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
    zmq::socket_t rep_socket(context, ZMQ_REP);
    std::string rep_endpoint = "ipc:///tmp/agent-bus-req";
    try {
        rep_socket.bind(rep_endpoint);
    } catch (const zmq::error_t& e) {
        log("ERROR", "Sample Extension: Failed to bind socket", "(" + rep_endpoint + "): " + std::to_string(e.num()));
        return 1;
    }
    
    int timeout = 1000;
    rep_socket.set(zmq::sockopt::rcvtimeo, timeout);
    
    log("INFO", "Sample Extension: Connected to ZeroMQ bus", "(" + rep_endpoint + ")");
    
    int request_count = 0;
    while (g_running) {
        zmq::message_t request_msg;
        if (!rep_socket.recv(request_msg, zmq::recv_flags::dontwait)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        std::string request_json(static_cast<const char*>(request_msg.data()), request_msg.size());
        Envelope req;
        if (!deserialize_envelope(request_json, req)) {
            log("ERROR", "Sample Extension: Failed to deserialize request");
            continue;
        }
        
        request_count++;
        log("INFO", "=== Request #" + std::to_string(request_count) + " ===");
        log("INFO", "  Topic:", req.topic);
        log("INFO", "  Correlation ID:", req.correlation_id);
        log("INFO", "  Payload:", req.payload_json);
        log("INFO", "  Timestamp:", std::to_string(req.ts_ms));
        
        // Log headers if present (v2)
        if (!req.headers.empty()) {
            log("INFO", "  Headers:");
            for (const auto& [key, value] : req.headers) {
                log("INFO", "    " + key + ":", value);
            }
        }
        
        // Log auth context if present (v2)
        if (!req.auth_context.uuid.empty()) {
            log("INFO", "  Auth Context:");
            log("INFO", "    Device Serial:", req.auth_context.device_serial);
            log("INFO", "    UUID:", req.auth_context.uuid);
            log("INFO", "    Cert Valid:", req.auth_context.cert_valid ? "true" : "false");
        }
        
        Envelope reply;
        reply.topic = req.topic + ".reply";
        reply.correlation_id = req.correlation_id;  // Preserve correlation ID
        reply.payload_json = R"({"status":"ok","message":"echo reply","requestPayload":)" + req.payload_json + "}";
        reply.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // Preserve headers and auth_context in reply (v2)
        reply.headers = req.headers;
        reply.auth_context = req.auth_context;
        
        std::string reply_json = serialize_envelope(reply);
        zmq::message_t reply_msg(reply_json.data(), reply_json.size());
        auto send_result = rep_socket.send(reply_msg, zmq::send_flags::dontwait);
        if (!send_result.has_value()) {
            log("ERROR", "Sample Extension: Failed to send reply");
        }
        
        log("INFO", "=== Reply #" + std::to_string(request_count) + " ===");
        log("INFO", "  Topic:", reply.topic);
        log("INFO", "  Correlation ID:", reply.correlation_id);
        log("INFO", "  Payload:", reply.payload_json);
        log("INFO", "  Timestamp:", std::to_string(reply.ts_ms));
    }
    
    log("INFO", "Sample Extension: Shutting down");
    return 0;
}
