#pragma once

#include <string>
#include <functional>
#include <memory>
#include "config.hpp"
#include "identity.hpp"

namespace agent {

struct MqttMsg {
    std::string topic;
    std::string payload;
    int qos{1};
};

class MqttClient {
public:
    virtual ~MqttClient() = default;
    
    // Connect to MQTT broker
    virtual bool connect(const Config& config, const Identity& identity) = 0;
    
    // Publish message
    virtual void publish(const MqttMsg& msg) = 0;
    
    //= Subscribe to topic with callback
    virtual void subscribe(const std::string& topic,
                          std::function<void(const MqttMsg&)> callback) = 0;
    
    // Disconnect from broker
    virtual void disconnect() = 0;
};

// Create MQTT client implementation
std::unique_ptr<MqttClient> create_mqtt_client();

}
