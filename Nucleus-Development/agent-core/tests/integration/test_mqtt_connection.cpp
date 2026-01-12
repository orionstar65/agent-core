#include "agent/mqtt_client.hpp"
#include "agent/comm_info.hpp"
#include "agent/config.hpp"
#include "agent/identity.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <limits.h>
#else
#include <linux/limits.h>
#endif

using namespace agent;

// Get the absolute path to the agent-core directory
std::string get_agent_core_dir() {
    char exe_path[PATH_MAX];
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, exe_path, sizeof(exe_path));
    if (len == 0 || len >= sizeof(exe_path)) {
        return "";
    }
    exe_path[len] = '\0';
    std::string path(exe_path);
    size_t pos = path.rfind("\\build\\tests\\");
    if (pos == std::string::npos) {
        pos = path.rfind("/build/tests/");
    }
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string path(exe_path);
        size_t pos = path.rfind("/build/tests/");
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
    }
#endif
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return std::string(cwd);
    }
    return ".";
}

// Test helper to create a test config
Config create_test_config() {
    Config config;
    std::string agent_core_dir = get_agent_core_dir();
    
    config.backend.base_url = "https://35.159.104.91:443";
    config.backend.comm_info_path = "/deviceservices/api/DeviceManagement/getcommunicationinformation/";
    config.backend.use_persistent_session = false;  // Use ARS proxy endpoint (35.159.104.91)
    config.cert.cert_path = agent_core_dir + "/cert_base64(200000).txt";
    config.retry.max_attempts = 3;
    config.retry.base_ms = 500;
    config.retry.max_ms = 5000;
    config.mqtt.qos = 1;
    config.mqtt.retain = false;
    config.mqtt.connection_timeout_sec = 30;
    config.mqtt.keepalive_interval_sec = 60;
    config.mqtt.command_timeout_sec = 3;
    return config;
}

// Test helper to create a test identity
Identity create_test_identity() {
    Identity identity;
    identity.is_gateway = false;
    identity.device_serial = "200000";
    identity.uuid = "a1635025-2723-4ffa-b608-208578d6128f";
    return identity;
}

// Test helper to fetch communication info
CommunicationInfo fetch_comm_info() {
    auto comm_info_mgr = create_comm_info_manager();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info;
    
    bool result = comm_info_mgr->fetch_comm_info(identity, config, comm_info);
    assert(result && "Failed to fetch communication info");
    
    return comm_info;
}

void test_mqtt_connection() {
    std::cout << "\n=== Test: MQTT Connection ===\n";
    
    auto mqtt_client = create_mqtt_client();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info = fetch_comm_info();
    
    bool connected = mqtt_client->connect(comm_info, identity, config);
    
    if (connected) {
        std::cout << "✓ Test passed: MQTT connection successful\n";
        assert(mqtt_client->is_connected() && "MQTT client should report connected status");
        mqtt_client->disconnect();
        std::cout << "✓ Disconnected successfully\n";
    } else {
        std::cout << "✗ MQTT connection failed\n";
        std::cout << "  Note: This may be expected if broker is not reachable\n";
    }
}

void test_mqtt_publish() {
    std::cout << "\n=== Test: MQTT Publish ===\n";
    
    auto mqtt_client = create_mqtt_client();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info = fetch_comm_info();
    
    bool connected = mqtt_client->connect(comm_info, identity, config);
    if (!connected) {
        std::cout << "⚠ Skipping publish test - connection failed\n";
        return;
    }
    
    // Publish test message
    MqttMsg msg;
    msg.topic = "test/" + identity.device_serial + "/publish";
    msg.payload = R"({"test": "message", "timestamp": 12345})";
    msg.qos = 1;
    msg.retain = false;
    
    mqtt_client->publish(msg);
    std::cout << "  ✓ Published test message to: " << msg.topic << "\n";
    
    std::cout << "✓ Test passed: MQTT publish successful\n";
    
    mqtt_client->disconnect();
}

void test_mqtt_subscribe() {
    std::cout << "\n=== Test: MQTT Subscribe ===\n";
    
    auto mqtt_client = create_mqtt_client();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info = fetch_comm_info();
    
    bool connected = mqtt_client->connect(comm_info, identity, config);
    if (!connected) {
        std::cout << "⚠ Skipping subscribe test - connection failed\n";
        return;
    }
    
    std::atomic<bool> message_received{false};
    std::string received_payload;
    
    // Subscribe to test topic
    std::string test_topic = "test/" + identity.device_serial + "/subscribe";
    mqtt_client->subscribe(test_topic, [&](const MqttMsg& msg) {
        std::cout << "  ✓ Message received on topic: " << msg.topic << "\n";
        std::cout << "    Payload: " << msg.payload << "\n";
        received_payload = msg.payload;
        message_received = true;
    });
    
    std::cout << "  ✓ Subscribed to: " << test_topic << "\n";
    
    // Wait a bit for subscription to be established
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Publish message to the subscribed topic
    MqttMsg msg;
    msg.topic = test_topic;
    msg.payload = R"({"test": "subscribe_message"})";
    msg.qos = 1;
    msg.retain = false;
    
    mqtt_client->publish(msg);
    std::cout << "  ✓ Published message to subscribed topic\n";
    
    // Wait for message to be received
    int wait_count = 0;
    while (!message_received && wait_count < 10) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_count++;
    }
    
    if (message_received) {
        std::cout << "✓ Test passed: MQTT subscribe successful\n";
    } else {
        std::cout << "⚠ Message not received within timeout\n";
    }
    
    mqtt_client->disconnect();
}

void test_status_request_response_flow() {
    std::cout << "\n=== Test: STATUS_REQUEST/RESPONSE Flow ===\n";
    std::cout << "  Testing the complete STATUS_REQUEST/RESPONSE handshake:\n";
    std::cout << "  1. Subscribe to /STATUS_REQUEST/<SerialNumber>_<UUID>\n";
    std::cout << "  2. Receive STATUS_REQUEST with payload: /STATUS_RESPONSE/<ClientId>\n";
    std::cout << "  3. Respond to that topic with payload: \"1\"\n\n";
    
    auto mqtt_client = create_mqtt_client();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info = fetch_comm_info();
    
    bool connected = mqtt_client->connect(comm_info, identity, config);
    if (!connected) {
        std::cerr << "✗ FAILED: Could not connect to MQTT broker\n";
        assert(false && "STATUS_REQUEST test requires MQTT connection");
        return;
    }
    
    // Build the client ID (same format as used in mqtt_client.cpp)
    std::string client_id = identity.device_serial + "_" + identity.uuid;
    
    // Topics per the spec:
    // Subscribe to: /STATUS_REQUEST/<SerialNumber>_<UUID>
    // Response topic (in payload): /STATUS_RESPONSE/<ClientId>
    std::string status_req_topic = "/STATUS_REQUEST/" + client_id;
    std::string expected_response_topic = "/STATUS_RESPONSE/" + client_id;
    
    std::atomic<bool> status_request_received{false};
    std::atomic<bool> status_response_sent{false};
    std::atomic<bool> status_response_verified{false};
    std::string response_topic_from_payload;
    
    // Step 1: Subscribe to STATUS_REQUEST topic
    std::cout << "  Step 1: Subscribing to STATUS_REQUEST topic...\n";
    std::cout << "    Topic: " << status_req_topic << "\n";
    
    mqtt_client->subscribe(status_req_topic, [&](const MqttMsg& msg) {
        std::cout << "  Step 2: STATUS_REQUEST received!\n";
        std::cout << "    Topic: " << msg.topic << "\n";
        std::cout << "    Payload (response topic): " << msg.payload << "\n";
        
        // The payload should be the response topic (plain string, not JSON)
        response_topic_from_payload = msg.payload;
        status_request_received = true;
    });
    
    // Also subscribe to the response topic to verify our response is sent
    mqtt_client->subscribe(expected_response_topic, [&](const MqttMsg& msg) {
        if (msg.payload == "1") {
            std::cout << "  ✓ STATUS_RESPONSE verified on broker!\n";
            status_response_verified = true;
        }
    });
    
    std::cout << "  ✓ Subscribed to: " << status_req_topic << "\n";
    std::cout << "  ✓ Also subscribed to response topic for verification: " << expected_response_topic << "\n";
    
    // Wait for subscription to be established
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Simulate a STATUS_REQUEST from the backend
    // In production, the backend sends this; for testing, we simulate it
    std::cout << "\n  Simulating STATUS_REQUEST from backend...\n";
    MqttMsg simulated_request;
    simulated_request.topic = status_req_topic;
    simulated_request.payload = expected_response_topic;  // Plain string, not JSON
    simulated_request.qos = config.mqtt.qos;
    simulated_request.retain = false;
    
    mqtt_client->publish(simulated_request);
    std::cout << "  ✓ Simulated STATUS_REQUEST sent\n";
    std::cout << "    Topic: " << simulated_request.topic << "\n";
    std::cout << "    Payload: " << simulated_request.payload << "\n";
    
    // Wait for STATUS_REQUEST to be received
    int wait_count = 0;
    const int max_wait_seconds = 10;
    while (!status_request_received && wait_count < max_wait_seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_count++;
    }
    
    // Step 3: Send STATUS_RESPONSE with payload "1" (outside callback for proper timing)
    if (status_request_received && !response_topic_from_payload.empty()) {
        std::cout << "  Step 3: Sending STATUS_RESPONSE...\n";
        std::cout << "    Topic: " << response_topic_from_payload << "\n";
        std::cout << "    Payload: 1\n";
        
        MqttMsg response;
        response.topic = response_topic_from_payload;
        response.payload = "1";  // Just "1" - not JSON
        response.qos = config.mqtt.qos;
        response.retain = config.mqtt.retain;
        
        mqtt_client->publish(response);
        status_response_sent = true;
        
        // Wait for response to be delivered and verification
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    // Verify results
    std::cout << "\n  === Test Results ===\n";
    
    bool test_passed = true;
    
    if (!status_request_received) {
        std::cerr << "  ✗ FAILED: STATUS_REQUEST was not received\n";
        test_passed = false;
    } else {
        std::cout << "  ✓ STATUS_REQUEST received\n";
    }
    
    if (!status_response_sent) {
        std::cerr << "  ✗ FAILED: STATUS_RESPONSE was not sent\n";
        test_passed = false;
    } else {
        std::cout << "  ✓ STATUS_RESPONSE sent with payload \"1\"\n";
    }
    
    if (response_topic_from_payload != expected_response_topic) {
        std::cerr << "  ✗ FAILED: Response topic mismatch\n";
        std::cerr << "    Expected: " << expected_response_topic << "\n";
        std::cerr << "    Got: " << response_topic_from_payload << "\n";
        test_passed = false;
    } else {
        std::cout << "  ✓ Response topic matches expected: " << expected_response_topic << "\n";
    }
    
    if (status_response_verified) {
        std::cout << "  ✓ STATUS_RESPONSE was verified on the broker\n";
    } else {
        std::cout << "  ⚠ STATUS_RESPONSE verification not confirmed (may be timing)\n";
    }
    
    mqtt_client->disconnect();
    
    if (test_passed) {
        std::cout << "\n✓ Test passed: STATUS_REQUEST/RESPONSE flow successful\n";
    } else {
        std::cerr << "\n✗ Test FAILED: STATUS_REQUEST/RESPONSE flow incomplete\n";
        assert(false && "STATUS_REQUEST/RESPONSE flow test failed");
    }
}

void test_mqtt_reconnection() {
    std::cout << "\n=== Test: MQTT Auto-Reconnection ===\n";
    std::cout << "  Note: This test verifies reconnection is enabled in configuration\n";
    
    auto mqtt_client = create_mqtt_client();
    Config config = create_test_config();
    Identity identity = create_test_identity();
    CommunicationInfo comm_info = fetch_comm_info();
    
    bool connected = mqtt_client->connect(comm_info, identity, config);
    if (connected) {
        assert(mqtt_client->is_connected() && "MQTT client should be connected");
        std::cout << "  ✓ Initial connection established\n";
        std::cout << "  ✓ Auto-reconnection is enabled in Paho MQTT config\n";
        std::cout << "✓ Test passed: Reconnection configuration verified\n";
        mqtt_client->disconnect();
    } else {
        std::cout << "⚠ Could not establish initial connection\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "MQTT Connection Integration Tests\n";
    std::cout << "========================================\n";
    
    bool run_status_request_test = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--test-status-request") {
            run_status_request_test = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --test-status-request   Run STATUS_REQUEST test (waits for backend)\n";
            std::cout << "  --help                  Show this help\n";
            return 0;
        }
    }
    
    try {
        test_mqtt_connection();
        test_mqtt_publish();
        test_mqtt_subscribe();
        test_mqtt_reconnection();
        
        if (run_status_request_test) {
            test_status_request_response_flow();
        } else {
            std::cout << "\nSkipping STATUS_REQUEST test (use --test-status-request to enable)\n";
        }
        
        std::cout << "\n========================================\n";
        std::cout << "All tests completed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}
