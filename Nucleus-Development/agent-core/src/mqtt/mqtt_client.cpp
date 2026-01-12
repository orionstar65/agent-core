#include "agent/mqtt_client.hpp"
#include "agent/comm_info.hpp"
#include "agent/aws_sigv4.hpp"
#include <iostream>
#include <map>
#include <thread>
#include <chrono>

// Paho MQTT C++ includes
#ifdef HAVE_PAHO_MQTT
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#endif

namespace agent {

#ifdef HAVE_PAHO_MQTT

// Callback class for MQTT events
class MqttCallback : public virtual mqtt::callback {
public:
    explicit MqttCallback(std::map<std::string, std::function<void(const MqttMsg&)>>& subscriptions)
        : subscriptions_(subscriptions) {}
    
    void connection_lost(const std::string& cause) override {
        std::cerr << "MqttClient: Connection lost";
        if (!cause.empty()) {
            std::cerr << ": " << cause;
        }
        std::cerr << "\n";
        std::cerr << "  - Paho will automatically attempt to reconnect...\n";
    }
    
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::string topic = msg->get_topic();
        std::string payload = msg->to_string();
        
        std::cout << "MqttClient: Message arrived on topic: " << topic << "\n";
        std::cout << "  - Payload: " << payload << "\n";
        
        // Find matching subscription callback
        auto it = subscriptions_.find(topic);
        if (it != subscriptions_.end()) {
            MqttMsg mqtt_msg;
            mqtt_msg.topic = topic;
            mqtt_msg.payload = payload;
            mqtt_msg.qos = msg->get_qos();
            
            it->second(mqtt_msg);
        } else {
            std::cout << "  - No callback registered for this topic\n";
        }
    }
    
private:
    std::map<std::string, std::function<void(const MqttMsg&)>>& subscriptions_;
};

class MqttClientImpl : public MqttClient {
public:
    MqttClientImpl() : client_(nullptr), callback_(nullptr) {}
    
    ~MqttClientImpl() {
        if (client_ && client_->is_connected()) {
            try {
                client_->disconnect()->wait();
            } catch (...) {
                // Ignore errors during cleanup
            }
        }
    }
    
    bool connect(const CommunicationInfo& comm_info,
                const Identity& identity,
                const Config& config) override {
        std::cout << "MqttClient: Connecting to MQTT Broker\n";
        std::cout << "  - Endpoint: " << comm_info.endpoint << "\n";
        std::cout << "  - Region: " << comm_info.region << "\n";
        std::cout << "  - Client ID: " << identity.device_serial << "_" << identity.uuid << "\n";
        
        try {
            // Client ID: SerialNumber_UUID
            std::string client_id = identity.device_serial + "_" + identity.uuid;
            
            // Build AWS credentials for SigV4 signing
            AwsCredentials aws_creds;
            aws_creds.access_key = comm_info.access_key;
            aws_creds.secret_key = comm_info.secret_key;
            aws_creds.session_token = comm_info.token;
            aws_creds.region = comm_info.region;
            
            // Generate SigV4 signed WebSocket URL
            std::string signed_url = sign_aws_iot_websocket_url(
                comm_info.endpoint,
                comm_info.region,
                aws_creds);
            
            // Show truncated URL for debugging
            std::cout << "  - Server URI: " << signed_url.substr(0, std::min(signed_url.length(), (size_t)80)) << "...\n";
            
            // Create async client with the signed URL
            client_ = std::make_unique<mqtt::async_client>(signed_url, client_id, nullptr);
            
            // Set callback
            callback_ = std::make_unique<MqttCallback>(subscriptions_);
            client_->set_callback(*callback_);
            
            // Build connection options
            mqtt::connect_options conn_opts;
            conn_opts.set_keep_alive_interval(config.mqtt.keepalive_interval_sec);
            conn_opts.set_clean_session(true);
            conn_opts.set_automatic_reconnect(true);
            
            // SSL options for WebSocket
            mqtt::ssl_options ssl_opts;
            ssl_opts.set_verify(false);  // May need to adjust based on broker
            ssl_opts.set_enable_server_cert_auth(false);
            conn_opts.set_ssl(ssl_opts);
            
            std::cout << "  - Auth: AWS SigV4 signed URL (WebSocket)\n";
            std::cout << "  - Connecting (timeout: " << config.mqtt.connection_timeout_sec << "s)...\n";
            
            // Connect with timeout
            auto tok = client_->connect(conn_opts);
            if (!tok->wait_for(std::chrono::seconds(config.mqtt.connection_timeout_sec))) {
                std::cerr << "MqttClient: Connection timeout after " 
                         << config.mqtt.connection_timeout_sec << " seconds\n";
                return false;
            }
            
            std::cout << "MqttClient: ✓ Connected to MQTT Broker\n";
            return true;
            
        } catch (const mqtt::exception& e) {
            std::cerr << "MqttClient: Connection failed: " << e.what() << "\n";
            return false;
        }
    }
    
    void publish(const MqttMsg& msg) override {
        if (!is_connected()) {
            std::cerr << "MqttClient: Not connected, cannot publish\n";
            return;
        }
        
        try {
            auto pub_msg = mqtt::make_message(msg.topic, msg.payload);
            pub_msg->set_qos(msg.qos);
            pub_msg->set_retained(msg.retain);
            
            std::cout << "MqttClient: Publishing to topic: " << msg.topic << "\n";
            std::cout << "  - Payload: " << msg.payload << "\n";
            std::cout << "  - QoS: " << msg.qos << "\n";
            std::cout << "  - Retain: " << (msg.retain ? "true" : "false") << "\n";
            
            client_->publish(pub_msg)->wait();
            
        } catch (const mqtt::exception& e) {
            std::cerr << "MqttClient: Publish failed: " << e.what() << "\n";
        }
    }
    
    void subscribe(const std::string& topic,
                   std::function<void(const MqttMsg&)> callback) override {
        if (!is_connected()) {
            std::cerr << "MqttClient: Not connected, cannot subscribe\n";
            return;
        }
        
        try {
            std::cout << "MqttClient: Subscribing to topic: " << topic << "\n";
            
            // Store callback
            subscriptions_[topic] = callback;
            
            // Subscribe with QoS 1
            client_->subscribe(topic, 1)->wait();
            
            std::cout << "  - Subscribed successfully\n";
            
        } catch (const mqtt::exception& e) {
            std::cerr << "MqttClient: Subscribe failed: " << e.what() << "\n";
        }
    }
    
    void disconnect() override {
        if (is_connected()) {
            try {
                std::cout << "MqttClient: Disconnecting...\n";
                client_->disconnect()->wait();
                std::cout << "  - Disconnected\n";
            } catch (const mqtt::exception& e) {
                std::cerr << "MqttClient: Disconnect failed: " << e.what() << "\n";
            }
        }
    }
    
    bool is_connected() const override {
        return client_ && client_->is_connected();
    }

private:
    std::unique_ptr<mqtt::async_client> client_;
    std::unique_ptr<MqttCallback> callback_;
    std::map<std::string, std::function<void(const MqttMsg&)>> subscriptions_;
};

#else  // !HAVE_PAHO_MQTT

// Stub implementation when Paho MQTT is not available
class MqttClientImpl : public MqttClient {
public:
    bool connect(const CommunicationInfo& comm_info,
                const Identity& identity,
                const Config& config) override {
        std::cerr << "MqttClient: PAHO_MQTT not available - cannot connect\n";
        std::cerr << "  - Please install Paho MQTT C++ library\n";
        std::cerr << "  - Endpoint: " << comm_info.endpoint << "\n";
        return false;
    }
    
    void publish(const MqttMsg& msg) override {
        std::cerr << "MqttClient: PAHO_MQTT not available - cannot publish\n";
    }
    
    void subscribe(const std::string& topic,
                   std::function<void(const MqttMsg&)> callback) override {
        std::cerr << "MqttClient: PAHO_MQTT not available - cannot subscribe\n";
    }
    
    void disconnect() override {
        // Nothing to do
    }
    
    bool is_connected() const override {
        return false;
    }
};

#endif  // HAVE_PAHO_MQTT

std::unique_ptr<MqttClient> create_mqtt_client() {
    return std::make_unique<MqttClientImpl>();
}

}
