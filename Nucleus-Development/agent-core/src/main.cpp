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
#include "agent/telemetry_collector.hpp"
#include "agent/telemetry_cache.hpp"
#include "agent/retry.hpp"
#include "agent/restart_manager.hpp"
#include "agent/restart_state_store.hpp"
#include "agent/service_installer.hpp"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <map>
#include <errno.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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
    AgentCore() : current_state_(AgentState::INIT), start_time_(std::chrono::steady_clock::now()) {}
    
    bool initialize(const std::string& config_path) {
        std::cout << "\n=== Agent Core v" << VERSION << " ===\n\n";
        
        // Create subsystems
        metrics_ = create_metrics();
        
        // Load configuration first to get logging config
        config_ = load_config(config_path);
        if (!config_) {
            std::cerr << "Failed to load configuration\n";
            return false;
        }
        
        // Create logger with throttling support
        if (config_->logging.throttle.enabled) {
            LoggingThrottleConfig throttle_cfg;
            throttle_cfg.enabled = config_->logging.throttle.enabled;
            throttle_cfg.error_threshold = config_->logging.throttle.error_threshold;
            throttle_cfg.window_seconds = config_->logging.throttle.window_seconds;
            
            logger_ = create_logger_with_throttle(
                config_->logging.level, 
                config_->logging.json,
                throttle_cfg,
                metrics_.get());
        } else {
            logger_ = create_logger(config_->logging.level, config_->logging.json);
        }
        
        log(LogLevel::Info, "Core", "Initializing Agent Core");
        
        // Load configuration
        current_state_ = AgentState::LOAD_CONFIG;
        log(LogLevel::Info, "Core", "Loading configuration from: " + config_path);
        
        // Create retry policy with metrics
        retry_policy_ = create_retry_policy(config_->retry, metrics_.get());
        
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
        bus_ = create_zmq_bus(logger_.get(), config_->zmq);
        mqtt_client_ = create_mqtt_client();
        ext_manager_ = create_extension_manager(config_->extensions);
        resource_monitor_ = create_resource_monitor();
        
        // Initialize telemetry if enabled
        if (config_->telemetry.enabled) {
            telemetry_collector_ = std::make_unique<TelemetryCollector>(
                resource_monitor_.get(),
                ext_manager_.get(),
                logger_.get(),
                metrics_.get(),
                *config_);
            
            telemetry_cache_ = std::make_unique<TelemetryCache>(
                *config_,
                mqtt_client_.get(),
                retry_policy_.get(),
                logger_.get(),
                metrics_.get(),
                identity_);
            
            log(LogLevel::Info, "Telemetry", "Telemetry system initialized");
        }
        
        log(LogLevel::Info, "Core", "Initialization complete");
        return true;
    }
    
    void run(ServiceHost& service_host, RestartManager* restart_mgr, RestartStateStore* restart_store) {
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
        
        // Setup ZeroMQ health query subscription
        bus_->subscribe("agent.health.query", 
            [this](const Envelope& req) {
                handle_health_query(req);
            });
        
        // Load and launch extensions from manifest
        auto ext_specs = load_extension_manifest(config_->extensions.manifest_path);
        if (!ext_specs.empty()) {
            ext_manager_->launch(ext_specs);
        }
        
        // Main run loop
        current_state_ = AgentState::RUNLOOP;
        log(LogLevel::Info, "Core", "Entering main run loop");
        
        const int stable_runtime_s = 300;
        bool restart_counter_reset = false;
        
        int loop_count = 0;
        while (!service_host.should_stop()) {
            // Check if stable runtime reached and reset restart counter
            if (!restart_counter_reset && restart_mgr) {
                auto runtime = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_).count();
                if (runtime >= stable_runtime_s) {
                    restart_mgr->reset();
                    if (restart_store) {
                        auto persisted = restart_mgr->to_persisted();
                        restart_store->save(persisted);
                    }
                    restart_counter_reset = true;
                }
            }
            
            // Heartbeat
            if (loop_count % 10 == 0) {
                send_heartbeat();
            }
            
            // Resource monitoring
            if (loop_count % 30 == 0) {
                check_resources();
            }
            
            // Extension monitoring (crash detection, restarts)
            if (loop_count % config_->extensions.crash_detection_interval_s == 0) {
                ext_manager_->monitor();
            }
            
            // Telemetry collection
            if (config_->telemetry.enabled && telemetry_collector_ && telemetry_cache_) {
                int sampling_interval = config_->telemetry.sampling_interval_s;
                if (loop_count - telemetry_sample_count_ >= sampling_interval) {
                    try {
                        auto batch = telemetry_collector_->collect();
                        telemetry_collector_->check_alerts(batch);
                        
                        telemetry_batch_queue_.push_back(batch);
                        telemetry_sample_count_ = loop_count;
                        
                        // Check if we should publish a batch
                        if (telemetry_batch_queue_.size() >= static_cast<size_t>(config_->telemetry.batch_size)) {
                            // Combine batches into single payload
                            TelemetryBatch combined_batch;
                            combined_batch.date_time = telemetry_batch_queue_.back().date_time;
                            
                            for (const auto& b : telemetry_batch_queue_) {
                                combined_batch.readings.insert(
                                    combined_batch.readings.end(),
                                    b.readings.begin(),
                                    b.readings.end());
                            }
                            
                            std::string json_payload = telemetry_collector_->to_json(combined_batch);
                            
                            // Attempt to publish
                            MqttMsg msg;
                            msg.topic = "/DeviceMonitoring/" + 
                                       (config_->telemetry.modality.empty() ? "CS" : config_->telemetry.modality) + "/" +
                                       (identity_.material_number.empty() ? "GATEWAY" : identity_.material_number) + "/" +
                                       (identity_.serial_number.empty() ? identity_.device_serial : identity_.serial_number);
                            msg.payload = json_payload;
                            msg.qos = 1;
                            
                            try {
                                mqtt_client_->publish(msg);
                                if (metrics_) {
                                    metrics_->increment("telemetry.published");
                                }
                                log(LogLevel::Debug, "Telemetry", "Published telemetry batch");
                            } catch (const std::exception& e) {
                                // Store in cache for retry
                                telemetry_cache_->store(json_payload);
                                log(LogLevel::Warn, "Telemetry", 
                                    "Failed to publish telemetry, cached for retry: " + std::string(e.what()));
                            }
                            
                            telemetry_batch_queue_.clear();
                        }
                    } catch (const std::exception& e) {
                        log(LogLevel::Error, "Telemetry", 
                            "Failed to collect telemetry: " + std::string(e.what()));
                    }
                }
                
                // Retry cached batches periodically (every 60 seconds)
                static int last_telemetry_retry = 0;
                if (loop_count - last_telemetry_retry >= 60) {
                    telemetry_cache_->retry_cached();
                    last_telemetry_retry = loop_count;
                }
            }
            
            // Extension health pings
            if (loop_count % config_->extensions.health_check_interval_s == 0) {
                ext_manager_->health_ping();
                check_extension_health();
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
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
    std::chrono::steady_clock::time_point start_time_;
    
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
    std::unique_ptr<TelemetryCollector> telemetry_collector_;
    std::unique_ptr<TelemetryCache> telemetry_cache_;
    
    int telemetry_sample_count_{0};
    std::vector<TelemetryBatch> telemetry_batch_queue_;
    
    void log(LogLevel level, const std::string& subsystem, const std::string& message, 
             const std::string& correlationId = "", const std::string& eventId = "") {
        if (logger_) {
            std::map<std::string, std::string> fields;
            std::string deviceId = identity_.device_serial.empty() ? 
                (identity_.is_gateway ? identity_.gateway_id : "") : identity_.device_serial;
            logger_->log(level, subsystem, message, fields, deviceId, correlationId, eventId);
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
            int64_t total_net = usage.net_in_kbps + usage.net_out_kbps;
            metrics_->gauge("network.usage", static_cast<double>(total_net));
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
    
    void handle_health_query(const Envelope& req) {
        log(LogLevel::Debug, "Health", "Received health query");
        
        // Get health status from extension manager
        auto health_map = ext_manager_->health_status();
        
        // Build JSON response using nlohmann::json library
        nlohmann::json json;
        json["extensions"] = nlohmann::json::array();
        
        for (const auto& [name, health] : health_map) {
            nlohmann::json ext_json;
            ext_json["name"] = name;
            ext_json["state"] = static_cast<int>(health.state);
            ext_json["restart_count"] = health.restart_count;
            ext_json["responding"] = health.responding;
            json["extensions"].push_back(ext_json);
        }
        
        json["agent_uptime_s"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count();
        
        std::string json_str = json.dump();
        
        // Send response via bus
        Envelope reply;
        reply.topic = req.topic + ".reply";
        reply.correlation_id = req.correlation_id;
        reply.payload_json = json_str;
        reply.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        bus_->publish(reply);
        
        if (metrics_) {
            metrics_->increment("health.queries");
        }
    }
};

int main(int argc, char* argv[]) {
    std::string config_path = "config/dev.json";
    std::string state_dir = "/var/lib/agent-core";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--state-dir" && i + 1 < argc) {
            state_dir = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --config PATH      Configuration file path (default: config/dev.json)\n"
                      << "  --state-dir PATH   State directory (default: /var/lib/agent-core)\n"
                      << "  --help             Show this help message\n";
            return 0;
        }
    }
    
    try {
        // Check if service is installed, if not, install it
        auto installer = create_service_installer();
        auto status = installer->check_status();
        
        if (status == ServiceInstallStatus::NotInstalled) {
            std::cout << "Agent Core: Service not installed, installing...\n";
            
            // Get current binary path
            char binary_path[1024];
#ifdef _WIN32
            GetModuleFileName(nullptr, binary_path, sizeof(binary_path));
#else
            ssize_t len = readlink("/proc/self/exe", binary_path, sizeof(binary_path) - 1);
            if (len != -1) {
                binary_path[len] = '\0';
            } else {
                std::cerr << "Failed to get binary path\n";
                return 1;
            }
#endif
            
            if (!installer->install(binary_path, config_path)) {
                std::cerr << "Failed to install service\n";
                return 1;
            }
            
            std::cout << "Agent Core: Service installed successfully\n";
            std::cout << "Agent Core: Starting service...\n";
            
            if (!installer->start()) {
                std::cerr << "Failed to start service\n";
                return 1;
            }
            
            std::cout << "Agent Core: Service started successfully\n";
            std::cout << "Agent Core: Exiting installer process\n";
            return 0;
        }
        
        // Load configuration first for restart policy
        auto config = load_config(config_path);
        if (!config) {
            std::cerr << "Failed to load configuration\n";
            return 1;
        }
        
        // Ensure state directory exists
#ifdef _WIN32
        if (_mkdir(state_dir.c_str()) != 0 && errno != EEXIST) {
#else
        if (mkdir(state_dir.c_str(), 0755) != 0 && errno != EEXIST) {
#endif
            std::cerr << "Failed to create state directory: " << state_dir << "\n";
            return 1;
        }
        
        // Handle restart management (catastrophic failure detection)
        std::string state_file = state_dir + "/restart-state.json";
        auto restart_store = create_restart_state_store(state_file);
        auto restart_mgr = create_restart_manager();
        
        // Load restart state from disk
        PersistedRestartState persisted_state;
        if (restart_store->exists() && restart_store->load(persisted_state)) {
            restart_mgr->load_from_persisted(persisted_state);
        }
        
        // Check if restart is allowed
        auto restart_decision = restart_mgr->should_restart(*config);
        
        if (restart_decision == RestartDecision::Quarantine) {
            std::cerr << "Agent Core: Too many restart attempts, entering quarantine for "
                      << config->service.quarantine_duration_s << " seconds\n";
            std::this_thread::sleep_for(std::chrono::seconds(config->service.quarantine_duration_s));
            return 1;
        } else if (restart_decision == RestartDecision::QuarantineActive) {
            std::cerr << "Agent Core: Currently in quarantine period\n";
            return 1;
        }
        
        // Apply backoff delay if this is a restart after failure
        if (persisted_state.restart_count > 0) {
            int delay_ms = restart_mgr->calculate_restart_delay_ms(*config);
            std::cout << "Agent Core: Applying restart backoff delay: " << delay_ms << "ms\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        
        // Record this restart attempt and persist state
        restart_mgr->record_restart();
        persisted_state = restart_mgr->to_persisted();
        restart_store->save(persisted_state);
        
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
            agent.run(*service_host, restart_mgr.get(), restart_store.get());
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
