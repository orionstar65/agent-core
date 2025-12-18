#include "agent/bus.hpp"
#include "agent/config.hpp"
#include "agent/extension_manager.hpp"
#include "agent/config.hpp"
#include "agent/telemetry.hpp"
#include "agent/uuid.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace agent;

Config::Extensions create_test_extension_config() {
    Config::Extensions config;
    config.max_restart_attempts = 3;
    config.restart_base_delay_ms = 1000;
    config.restart_max_delay_ms = 60000;
    config.quarantine_duration_s = 300;
    return config;
}

// Test helper to create a test extension spec
ExtensionSpec create_test_extension_spec() {
    ExtensionSpec spec;
    spec.name = "sample-ext";
    // Test executable is at: build/tests/test_zmq
    // Extension should be at: ../../../extensions/sample/build/sample-ext
    spec.exec_path = "../../../extensions/sample/build/sample-ext";
    spec.args = {};
    spec.critical = false;
    return spec;
}

void test_zmq_request_reply() {
    std::cout << "\n=== Test: ZeroMQ Request/Reply ===\n";
    
    auto logger = create_logger("info", false);
    Config::ZeroMQ zmq_config;
    zmq_config.pub_port = 5555;
    zmq_config.req_port = 5556;
    auto bus = create_zmq_bus(logger.get(), zmq_config);
    auto ext_manager = create_extension_manager(create_test_extension_config());
    
    ExtensionSpec spec = create_test_extension_spec();
    std::vector<ExtensionSpec> ext_specs;
    ext_specs.push_back(spec);
    ext_manager->launch(ext_specs);
    
    // Wait for extension to be ready
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    Envelope req;
    req.topic = "ext.sample.echo";
    req.correlation_id = util::generate_uuid();
    req.payload_json = R"({"message":"Hello from integration test","test":true})";
    req.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    Envelope reply;
    bus->request(req, reply);
    
    assert(reply.correlation_id == req.correlation_id && 
           "Correlation ID should match");
    assert(reply.topic == req.topic + ".reply" && 
           "Reply topic should be request topic + .reply");
    
    ext_manager->stop_all();
    std::cout << "✓ Test passed: ZeroMQ request/reply successful\n";
}

void test_zmq_correlation_id_preservation() {
    std::cout << "\n=== Test: Correlation ID Preservation ===\n";
    
    auto logger = create_logger("info", false);
    Config::ZeroMQ zmq_config;
    zmq_config.pub_port = 5555;
    zmq_config.req_port = 5556;
    auto bus = create_zmq_bus(logger.get(), zmq_config);
    auto ext_manager = create_extension_manager(create_test_extension_config());
    
    ExtensionSpec spec = create_test_extension_spec();
    std::vector<ExtensionSpec> ext_specs;
    ext_specs.push_back(spec);
    ext_manager->launch(ext_specs);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    Envelope req;
    req.topic = "ext.sample.echo";
    req.correlation_id = "test-correlation-id-12345";
    req.payload_json = R"({"message":"test"})";
    req.ts_ms = 0;
    
    Envelope reply;
    bus->request(req, reply);
    
    assert(reply.correlation_id == req.correlation_id && 
           "Correlation ID should be preserved");
    
    ext_manager->stop_all();
    std::cout << "✓ Test passed: Correlation ID preserved\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "ZeroMQ Integration Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_zmq_request_reply();
        test_zmq_correlation_id_preservation();
        
        std::cout << "\n========================================\n";
        std::cout << "All tests passed!\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n========================================\n";
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        std::cerr << "========================================\n";
        return 1;
    }
}

