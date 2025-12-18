#include "agent/bus.hpp"
#include "agent/envelope_serialization.hpp"
#include "agent/config.hpp"
#include "agent/telemetry.hpp"
#include <stdexcept>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef HAVE_ZMQ
#include <zmq.hpp>
#endif

namespace agent {

class ZmqBusImpl : public Bus {
public:
    ZmqBusImpl(Logger* logger, int pub_port, int req_port,
                bool curve_enabled = false,
                const std::string& curve_server_key = "",
                const std::string& curve_public_key = "",
                const std::string& curve_secret_key = "") 
        : logger_(logger), pub_port_(pub_port), req_port_(req_port),
          curve_enabled_(curve_enabled), curve_server_key_(curve_server_key),
          curve_public_key_(curve_public_key), curve_secret_key_(curve_secret_key) {
#ifdef HAVE_ZMQ
        context_ = std::make_unique<zmq::context_t>(1);
        
        pub_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        bool pub_is_tcp = false;
#ifdef _WIN32
        // Windows: ZeroMQ IPC doesn't work well, use TCP localhost instead
        std::string pub_endpoint = "tcp://127.0.0.1:" + std::to_string(pub_port_);
        pub_is_tcp = true;
#else
        // Linux: Use /tmp/ directory for IPC
        // Note: IPC sockets are cleaned up automatically when the process exits
        std::string pub_endpoint = "ipc:///tmp/agent-bus-pub";
        pub_is_tcp = false;
#endif
        
        // Apply CURVE encryption for TCP (inter-process) connections
        if (curve_enabled_ && pub_is_tcp) {
            if (!curve_server_key_.empty()) {
                pub_socket_->set(zmq::sockopt::curve_server, 1);
                pub_socket_->set(zmq::sockopt::curve_secretkey, curve_server_key_);
            } else {
                if (logger_) {
                    logger_->log(LogLevel::Warn, "Bus", "CURVE enabled but no server key provided for PUB socket", {});
                }
            }
        }
        
        try {
            pub_socket_->bind(pub_endpoint);
        } catch (const zmq::error_t& e) {
            if (logger_) {
                logger_->log(LogLevel::Error, "Bus", "Failed to bind pub socket", 
                    {{"endpoint", pub_endpoint}, {"error", std::to_string(e.num())}});
            }
            throw std::runtime_error("Failed to bind pub socket: " + std::to_string(e.num()));
        }
        
        req_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_REQ);
        bool req_is_tcp = false;
#ifdef _WIN32
        // Windows: ZeroMQ IPC doesn't work well, use TCP localhost instead
        std::string req_endpoint = "tcp://127.0.0.1:" + std::to_string(req_port_);
        req_is_tcp = true;
#else
        // Linux: Use /tmp/ directory for IPC
        // Note: IPC sockets are cleaned up automatically when the process exits
        std::string req_endpoint = "ipc:///tmp/agent-bus-req";
        req_is_tcp = false;
#endif
        
        // Apply CURVE encryption for TCP (inter-process) connections
        if (curve_enabled_ && req_is_tcp) {
            if (!curve_server_key_.empty() && !curve_public_key_.empty() && !curve_secret_key_.empty()) {
                req_socket_->set(zmq::sockopt::curve_serverkey, curve_server_key_);
                req_socket_->set(zmq::sockopt::curve_publickey, curve_public_key_);
                req_socket_->set(zmq::sockopt::curve_secretkey, curve_secret_key_);
            } else {
                if (logger_) {
                    logger_->log(LogLevel::Warn, "Bus", "CURVE enabled but keys not provided for REQ socket", {});
                }
            }
        }
        
        try {
            req_socket_->connect(req_endpoint);
        } catch (const zmq::error_t& e) {
            if (logger_) {
                logger_->log(LogLevel::Error, "Bus", "Failed to connect req socket", 
                    {{"endpoint", req_endpoint}, {"error", std::to_string(e.num())}});
            }
            throw std::runtime_error("Failed to connect req socket: " + std::to_string(e.num()));
        }
        
        int linger = 0;
        pub_socket_->set(zmq::sockopt::linger, linger);
        req_socket_->set(zmq::sockopt::linger, linger);
        
        int timeout = 5000;
        req_socket_->set(zmq::sockopt::rcvtimeo, timeout);
        req_socket_->set(zmq::sockopt::sndtimeo, timeout);
        
        if (logger_) {
            std::map<std::string, std::string> log_fields = {
                {"pub_endpoint", pub_endpoint}, 
                {"req_endpoint", req_endpoint},
                {"curve_enabled", curve_enabled_ ? "true" : "false"}
            };
            logger_->log(LogLevel::Info, "Bus", "ZeroMQ bus initialized", log_fields);
        }
#else
        if (logger_) {
            logger_->log(LogLevel::Warn, "Bus", "ZeroMQ not available - using stub implementation", {});
        }
#endif
    }
    
    ~ZmqBusImpl() override {
#ifdef HAVE_ZMQ
        running_ = false;
        if (sub_thread_.joinable()) {
            sub_thread_.join();
        }
#endif
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Shutting down", {});
        }
    }
    
    void publish(const Envelope& envelope) override {
#ifdef HAVE_ZMQ
        std::string json = serialize_envelope(envelope);
        zmq::message_t topic_msg(envelope.topic.data(), envelope.topic.size());
        zmq::message_t payload_msg(json.data(), json.size());
        
        pub_socket_->send(topic_msg, zmq::send_flags::sndmore);
        pub_socket_->send(payload_msg, zmq::send_flags::dontwait);
        
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Published message", 
                {{"topic", envelope.topic}}, "", envelope.correlation_id);
        }
#else
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Published message (stub)", 
                {{"topic", envelope.topic}}, "", envelope.correlation_id);
        }
#endif
    }
    
    void request(const Envelope& req, Envelope& reply) override {
#ifdef HAVE_ZMQ
        std::string json = serialize_envelope(req);
        zmq::message_t request_msg(json.data(), json.size());
        
        auto send_result = req_socket_->send(request_msg, zmq::send_flags::none);
        if (!send_result.has_value()) {
            throw std::runtime_error("Failed to send request");
        }
        
        zmq::message_t reply_msg;
        auto recv_result = req_socket_->recv(reply_msg, zmq::recv_flags::none);
        if (!recv_result.has_value()) {
            throw std::runtime_error("Failed to receive reply (timeout or error)");
        }
        
        std::string reply_json(static_cast<const char*>(reply_msg.data()), reply_msg.size());
        if (!deserialize_envelope(reply_json, reply)) {
            throw std::runtime_error("Failed to deserialize reply");
        }
        
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Request completed", 
                {{"topic", req.topic}, {"replyCorrelationId", reply.correlation_id}}, 
                "", req.correlation_id);
        }
#else
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Request (stub)", 
                {{"topic", req.topic}}, "", req.correlation_id);
        }
        reply.topic = req.topic + ".reply";
        reply.correlation_id = req.correlation_id;
        reply.payload_json = R"({"status": "ok", "message": "stub reply"})";
        reply.ts_ms = req.ts_ms;
#endif
    }
    
    // Helper function to check if a topic matches a pattern
    static bool topic_matches(const std::string& topic, const std::string& pattern) {
        // Exact match
        if (topic == pattern) {
            return true;
        }
        
        // Wildcard pattern: convert "ext.ps.*" to prefix "ext.ps."
        if (pattern.back() == '*') {
            std::string prefix = pattern.substr(0, pattern.length() - 1);
            if (topic.length() >= prefix.length() && 
                topic.substr(0, prefix.length()) == prefix) {
                return true;
            }
        }
        
        // Prefix pattern: "ext.ps." matches "ext.ps.exec.req"
        if (pattern.back() == '.' || pattern.back() == '/') {
            if (topic.length() >= pattern.length() && 
                topic.substr(0, pattern.length()) == pattern) {
                return true;
            }
        }
        
        return false;
    }
    
    // Convert pattern to ZeroMQ subscription format
    static std::string pattern_to_zmq_filter(const std::string& pattern) {
        // Wildcard: "ext.ps.*" -> "ext.ps." (prefix for ZeroMQ)
        if (pattern.back() == '*') {
            return pattern.substr(0, pattern.length() - 1);
        }
        // Prefix pattern already works with ZeroMQ
        // Exact match: use as-is (ZeroMQ will do exact match)
        return pattern;
    }
    
    void subscribe(const std::string& topic,
                   std::function<void(const Envelope&)> callback) override {
#ifdef HAVE_ZMQ
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            subscriptions_[topic] = callback;
            
            // Start subscriber thread if not already running
            if (!running_) {
        running_ = true;
        
                // Launch subscriber thread
                sub_thread_ = std::thread([this]() {
            zmq::socket_t sub_socket(*context_, ZMQ_SUB);
                    bool sub_is_tcp = false;
#ifdef _WIN32
            // Windows: ZeroMQ IPC doesn't work well, use TCP localhost instead
            std::string sub_endpoint = "tcp://127.0.0.1:" + std::to_string(pub_port_);
                    sub_is_tcp = true;
#else
            // Linux: Use /tmp/ directory for IPC
                    // Note: IPC sockets are cleaned up automatically when the process exits
            std::string sub_endpoint = "ipc:///tmp/agent-bus-pub";
                    sub_is_tcp = false;
#endif
                    
                    // Apply CURVE encryption for TCP (inter-process) connections
                    if (curve_enabled_ && sub_is_tcp) {
                        if (!curve_server_key_.empty() && !curve_public_key_.empty() && !curve_secret_key_.empty()) {
                            sub_socket.set(zmq::sockopt::curve_serverkey, curve_server_key_);
                            sub_socket.set(zmq::sockopt::curve_publickey, curve_public_key_);
                            sub_socket.set(zmq::sockopt::curve_secretkey, curve_secret_key_);
                        } else {
                            if (logger_) {
                                logger_->log(LogLevel::Warn, "Bus", "CURVE enabled but keys not provided for SUB socket", {});
                            }
                        }
                    }
                    
            try {
                sub_socket.connect(sub_endpoint);
            } catch (const zmq::error_t& e) {
                if (logger_) {
                    logger_->log(LogLevel::Error, "Bus", "Failed to connect sub socket", 
                        {{"endpoint", sub_endpoint}, {"error", std::to_string(e.num())}});
                }
                return;
            }
                    
                    // Subscribe to all patterns (ZeroMQ will filter by prefix)
                    {
                        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                        for (const auto& pair : subscriptions_) {
                            std::string zmq_filter = pattern_to_zmq_filter(pair.first);
                            sub_socket.set(zmq::sockopt::subscribe, zmq_filter);
                        }
                    }
            
            int timeout = 1000;
            sub_socket.set(zmq::sockopt::rcvtimeo, timeout);
            
            while (running_) {
                zmq::message_t topic_msg;
                auto topic_result = sub_socket.recv(topic_msg, zmq::recv_flags::dontwait);
                if (!topic_result.has_value()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                
                zmq::message_t payload_msg;
                auto payload_result = sub_socket.recv(payload_msg, zmq::recv_flags::dontwait);
                if (!payload_result.has_value()) {
                    continue;
                }
                
                std::string topic_str(static_cast<const char*>(topic_msg.data()), topic_msg.size());
                std::string json_str(static_cast<const char*>(payload_msg.data()), payload_msg.size());
                
                        // Match topic against all subscription patterns
                        std::vector<std::function<void(const Envelope&)>> matching_callbacks;
                {
                    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                            for (const auto& pair : subscriptions_) {
                                if (topic_matches(topic_str, pair.first)) {
                                    matching_callbacks.push_back(pair.second);
                                }
                    }
                }
                
                        // Call all matching callbacks
                    Envelope envelope;
                    if (deserialize_envelope(json_str, envelope)) {
                            for (const auto& cb : matching_callbacks) {
                                cb(envelope);
                    }
                }
            }
        });
            } else {
                // Thread already running, add new subscription filter
                // Note: ZeroMQ subscriptions are additive, but we need to re-subscribe
                // This is a limitation - new subscriptions won't be active until restart
                // For production, consider using a more sophisticated approach
            }
        }
        
        if (logger_) {
            logger_->log(LogLevel::Info, "Bus", "Subscribed to topic", {{"topic", topic}});
        }
#else
        if (logger_) {
            logger_->log(LogLevel::Info, "Bus", "Subscribed to topic (stub)", {{"topic", topic}});
        }
        subscriptions_[topic] = callback;
#endif
    }

private:
    Logger* logger_;
    int pub_port_;
    int req_port_;
    bool curve_enabled_;
    std::string curve_server_key_;
    std::string curve_public_key_;
    std::string curve_secret_key_;
#ifdef HAVE_ZMQ
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> pub_socket_;
    std::unique_ptr<zmq::socket_t> req_socket_;
#endif
    std::map<std::string, std::function<void(const Envelope&)>> subscriptions_;
    std::mutex subscriptions_mutex_;
#ifdef HAVE_ZMQ
    std::thread sub_thread_;
    std::atomic<bool> running_{false};
#endif
};

std::unique_ptr<Bus> create_zmq_bus(Logger* logger, const Config::ZeroMQ& zmq_config) {
    return std::make_unique<ZmqBusImpl>(logger, zmq_config.pub_port, zmq_config.req_port,
                                        zmq_config.curve_enabled, zmq_config.curve_server_key,
                                        zmq_config.curve_public_key, zmq_config.curve_secret_key);
}

AuthContext create_auth_context(const Identity& identity, CertState cert_state, int64_t cert_expires_ms) {
    AuthContext ctx;
    ctx.device_serial = identity.device_serial;
    ctx.gateway_id = identity.gateway_id;
    ctx.uuid = identity.uuid;
    ctx.cert_valid = (cert_state == CertState::Valid || cert_state == CertState::Renewed);
    ctx.cert_expires_ms = cert_expires_ms;
    return ctx;
}

}
