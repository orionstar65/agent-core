#include "agent/bus.hpp"
#include "agent/envelope_serialization.hpp"
#include "agent/telemetry.hpp"
#include <stdexcept>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

#ifdef HAVE_ZMQ
#include <zmq.hpp>
#endif

namespace agent {

class ZmqBusImpl : public Bus {
public:
    ZmqBusImpl(Logger* logger) : logger_(logger) {
#ifdef HAVE_ZMQ
        context_ = std::make_unique<zmq::context_t>(1);
        
        pub_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        std::string pub_endpoint = "ipc:///tmp/agent-bus-pub";
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
        std::string req_endpoint = "ipc:///tmp/agent-bus-req";
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
            logger_->log(LogLevel::Info, "Bus", "ZeroMQ bus initialized", 
                {{"pub_endpoint", pub_endpoint}, {"req_endpoint", req_endpoint}});
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
                {{"topic", envelope.topic}, {"correlationId", envelope.correlation_id}});
        }
#else
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Published message (stub)", 
                {{"topic", envelope.topic}, {"correlationId", envelope.correlation_id}});
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
                {{"topic", req.topic}, {"correlationId", req.correlation_id}, 
                 {"replyCorrelationId", reply.correlation_id}});
        }
#else
        if (logger_) {
            logger_->log(LogLevel::Debug, "Bus", "Request (stub)", 
                {{"topic", req.topic}, {"correlationId", req.correlation_id}});
        }
        reply.topic = req.topic + ".reply";
        reply.correlation_id = req.correlation_id;
        reply.payload_json = R"({"status": "ok", "message": "stub reply"})";
        reply.ts_ms = req.ts_ms;
#endif
    }
    
    void subscribe(const std::string& topic,
                   std::function<void(const Envelope&)> callback) override {
#ifdef HAVE_ZMQ
        if (sub_thread_.joinable()) {
            throw std::runtime_error("Subscribe already called");
        }
        
        {
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            subscriptions_[topic] = callback;
        }
        running_ = true;
        
        sub_thread_ = std::thread([this, topic]() {
            zmq::socket_t sub_socket(*context_, ZMQ_SUB);
            std::string sub_endpoint = "ipc:///tmp/agent-bus-pub";
            try {
                sub_socket.connect(sub_endpoint);
            } catch (const zmq::error_t& e) {
                if (logger_) {
                    logger_->log(LogLevel::Error, "Bus", "Failed to connect sub socket", 
                        {{"endpoint", sub_endpoint}, {"error", std::to_string(e.num())}});
                }
                return;
            }
            sub_socket.set(zmq::sockopt::subscribe, topic);
            
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
                
                std::function<void(const Envelope&)> callback;
                {
                    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                    auto it = subscriptions_.find(topic_str);
                    if (it != subscriptions_.end()) {
                        callback = it->second;
                    }
                }
                
                if (callback) {
                    Envelope envelope;
                    if (deserialize_envelope(json_str, envelope)) {
                        callback(envelope);
                    }
                }
            }
        });
        
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

std::unique_ptr<Bus> create_zmq_bus(Logger* logger) {
    return std::make_unique<ZmqBusImpl>(logger);
}

}
