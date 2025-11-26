#include "agent/version.hpp"
#include "agent/config.hpp"
#include "agent/service_host.hpp"
#include "agent/identity.hpp"
#include "agent/net_path_selector.hpp"
#include "agent/auth_manager.hpp"
#include "agent/registration.hpp"
#include "agent/mqtt_client.hpp"
#include "agent/bus.hpp"
#include "agent/extension_manager.hpp"
#include "agent/resource_monitor.hpp"
#include "agent/telemetry.hpp"
#include "agent/retry.hpp"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <map>

using namespace agent;

enum class AgentState {
    INIT,
    LOAD_CONFIG,
    IDENTITY_RESOLVE,
    NET_DECIDE,
    AUTH,
    REGISTER,
    MQTT_CONNECT,
    RUNLOOP,
    SHUTDOWN
};

class AgentCore {
public:
    AgentCore() : current_state_(AgentState::INIT) {}
    
    bool initialize(const std::string& config_path) {
        std::cout << "\n=== Agent Core v" << VERSION << " ===\n\n";
        
        // Create subsystems
        logger_ = create_logger("info", false);
        metrics_ = create_metrics();
        
        log(LogLevel::Info, "Core", "Initializing Agent Core");
        
        // Load configuration
        current_state_ = AgentState::LOAD_CONFIG;
        log(LogLevel::Info, "Core", "Loading configuration from: " + config_path);
        
        config_ = load_config(config_path);
        if (!config_) {
            log(LogLevel::Error, "Core", "Failed to load configuration");
            return false;
        }
        
        // Create retry policy
        retry_policy_ = create_retry_policy(config_->retry);
        
        // Discover identity
        current_state_ = AgentState::IDENTITY_RESOLVE;
        log(LogLevel::Info, "Core", "Discovering identity");
        
        identity_ = discover_identity(*config_);
        
        // Network path decision
        current_state_ = AgentState::NET_DECIDE;
        log(LogLevel::Info, "Core", "Determining network path");
        
        auto net_selector = create_net_path_selector();
        auto net_decision = net_selector->decide(*config_, identity_);
        
        if (net_decision.path == Path::Tunnel) {
            log(LogLevel::Info, "Core", "Tunnel path required - would launch tunnel extension");
            // TODO: Launch tunnel extension and wait for TunnelReady
        }
        
        // Authentication
        current_state_ = AgentState::AUTH;
        log(LogLevel::Info, "Core", "Ensuring certificate validity");
        
        auto auth_mgr = create_auth_manager();
        auto cert_state = auth_mgr->ensure_certificate(identity_, *config_);
        
        if (cert_state == CertState::Failed) {
            log(LogLevel::Error, "Core", "Certificate validation failed");
            return false;
        }
        
        // Registration
        current_state_ = AgentState::REGISTER;
        log(LogLevel::Info, "Core", "Registering with backend");
        
        registration_ = create_ssm_registration();
        auto reg_state = registration_->register_device(identity_, *config_);
        
        if (reg_state == RegistrationState::Failed) {
            log(LogLevel::Error, "Core", "Registration failed");
            return false;
        }
        
        // Initialize subsystems
        bus_ = create_zmq_bus();
        mqtt_client_ = create_mqtt_client();
        ext_manager_ = create_extension_manager();
        resource_monitor_ = create_resource_monitor();
        
        log(LogLevel::Info, "Core", "Initialization complete");
        return true;
    }
    
    void run(ServiceHost& service_host) {
        // MQTT Connection
        current_state_ = AgentState::MQTT_CONNECT;
        log(LogLevel::Info, "Core", "Connecting to MQTT broker");
        
        if (!mqtt_client_->connect(*config_, identity_)) {
            log(LogLevel::Error, "Core", "MQTT connection failed");
            return;
        }
        
        // Setup MQTT subscriptions
        std::string cmd_topic = "device/" + identity_.device_serial + "/commands";
        mqtt_client_->subscribe(cmd_topic, 
            [this](const MqttMsg& msg) {
                handle_command(msg);
            });
        
        // launch extensions (if any configured)
        // for now launch a dummy extension spec
        std::vector<ExtensionSpec> ext_specs;
        // this is an example: ext_specs.push_back({"sample-ext", "./sample-ext", {}, false});
        
        if (!ext_specs.empty()) {
            ext_manager_->launch(ext_specs);
        }
        
        // Main run loop
        current_state_ = AgentState::RUNLOOP;
        log(LogLevel::Info, "Core", "Entering main run loop");
        
        int loop_count = 0;
        while (!service_host.should_stop()) {
            // Heartbeat
            if (loop_count % 10 == 0) {
                send_heartbeat();
            }
            
            // Resource monitoring
            if (loop_count % 30 == 0) {
                check_resources();
            }
            
            // Extension health checks
            if (loop_count % 20 == 0) {
                check_extension_health();
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // TODO: Need to determine the number of seconds between checks
            loop_count++;
        }
        
        log(LogLevel::Info, "Core", "Main loop exited");
    }
    
    void shutdown() {
        current_state_ = AgentState::SHUTDOWN;
        log(LogLevel::Info, "Core", "Shutting down Agent Core");
        
        // Stop extensions
        // TODO: might need to only stop certian extensions
        if (ext_manager_) {
            ext_manager_->stop_all();
        }
        
        // Disconnect MQTT
        if (mqtt_client_) {
            mqtt_client_->disconnect();
        }
        
        log(LogLevel::Info, "Core", "Shutdown complete");
    }

private:
    AgentState current_state_;
    
    std::unique_ptr<Config> config_;
    Identity identity_;
    
    std::unique_ptr<Logger> logger_;
    std::unique_ptr<Metrics> metrics_;
    std::unique_ptr<RetryPolicy> retry_policy_;
    std::unique_ptr<Bus> bus_;
    std::unique_ptr<MqttClient> mqtt_client_;
    std::unique_ptr<Registration> registration_;
    std::unique_ptr<ExtensionManager> ext_manager_;
    std::unique_ptr<ResourceMonitor> resource_monitor_;
    
    void log(LogLevel level, const std::string& subsystem, const std::string& message) {
        if (logger_) {
            std::map<std::string, std::string> fields;
            fields["deviceId"] = identity_.device_serial;
            logger_->log(level, subsystem, message, fields);
        }
    }
    
    void send_heartbeat() {
        log(LogLevel::Debug, "Heartbeat", "Sending heartbeat");
        
        MqttMsg msg;
        msg.topic = "device/" + identity_.device_serial + "/heartbeat";
        msg.payload = R"({"status": "alive", "timestamp": 0})";
        msg.qos = 0;
        
        mqtt_client_->publish(msg);
        
        if (metrics_) {
            metrics_->increment("heartbeat.sent");
        }
    }
    
    void check_resources() {
        log(LogLevel::Debug, "Resources", "Checking resource usage");
        
        auto usage = resource_monitor_->sample("agent-core");
        
        if (metrics_) {
            metrics_->gauge("cpu.usage", usage.cpu_pct);
            metrics_->gauge("memory.usage", usage.mem_mb);
            metrics_->gauge("network.usage", usage.net_kbps);
        }
        
        if (resource_monitor_->exceeds_budget(usage, *config_)) {
            log(LogLevel::Warn, "Resources", "Resource usage exceeds budget");
        }
    }
    
    void check_extension_health() {
        auto statuses = ext_manager_->status();
        
        for (const auto& [name, state] : statuses) {
            if (state == ExtState::Crashed) {
                log(LogLevel::Error, "Extensions", "Extension crashed: " + name);
                if (metrics_) {
                    metrics_->increment("extension.crashes");
                }
            }
        }
    }
    
    void handle_command(const MqttMsg& msg) {
        log(LogLevel::Info, "Command", "Received command on topic: " + msg.topic);
        
        // TODO: Parse command and route to appropriate extension via bus
        
        if (metrics_) {
            metrics_->increment("commands.received");
        }
    }
};

int main(int argc, char* argv[]) {
    std::string config_path = "config/dev.json";
    
    // Parse command line arguments
    // TODO: will need to determine the command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --config PATH    Configuration file path (default: config/dev.json)\n"
                      << "  --help           Show this help message\n";
            return 0;
        }
    }
    
    try {
        // Create service host
        auto service_host = create_service_host();
        if (!service_host->initialize()) {
            std::cerr << "Failed to initialize service host\n";
            return 1;
        }
        
        // Create agent core
        AgentCore agent;
        if (!agent.initialize(config_path)) {
            std::cerr << "Failed to initialize agent core\n";
            return 1;
        }
        
        // Run main loop
        service_host->run([&]() {
            agent.run(*service_host);
        });
        
        // Cleanup
        agent.shutdown();
        service_host->shutdown();
        
        std::cout << "Agent Core exited cleanly\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
