#include "agent/comm_info.hpp"
#include "agent/config.hpp"
#include "agent/identity.hpp"
#include "agent/https_client.hpp"
#include "agent/retry.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace agent {

class CommInfoManagerImpl : public CommInfoManager {
public:
    CommInfoManagerImpl() : https_client_(create_https_client()) {}
    
    bool fetch_comm_info(const Identity& identity,
                        const Config& config,
                        CommunicationInfo& comm_info) override {
        std::cout << "CommInfoManager: Fetching AWS IoT credentials for "
                  << (identity.is_gateway ? "gateway " + identity.gateway_id
                                          : "device " + identity.device_serial)
                  << "\n";
        
        // Get serial number and UUID from identity
        std::string serial_number = identity.device_serial;
        std::string uuid = identity.uuid;
        
        if (serial_number.empty()) {
            std::cerr << "CommInfoManager: ERROR - Device serial number is empty\n";
            return false;
        }
        
        if (uuid.empty()) {
            std::cerr << "CommInfoManager: ERROR - UUID is empty\n";
            return false;
        }
        
        // Read certificate from file
        std::string cert_content = read_certificate(config.cert.cert_path);
        if (cert_content.empty()) {
            std::cerr << "CommInfoManager: ERROR - Failed to read certificate from: "
                      << config.cert.cert_path << "\n";
            return false;
        }
        
        // Build communication info URL
        // {baseUrl}/deviceservices/api/DeviceManagement/getcommunicationinformation/{serialNumber}/{bool}
        std::string comm_info_url = config.backend.base_url + 
                                     config.backend.comm_info_path +
                                     serial_number + "/" +
                                     (config.backend.use_persistent_session ? "true" : "false");
        
        std::cout << "  - Serial Number: " << serial_number << "\n";
        std::cout << "  - UUID: " << uuid << "\n";
        std::cout << "  - Communication Info URL: " << comm_info_url << "\n";
        std::cout << "  - Use Persistent Session: " 
                  << (config.backend.use_persistent_session ? "true" : "false") << "\n";
        
        // Build request body (same as auth_manager)
        std::ostringstream body;
        body << "{\n"
             << "  \"serialNumber\": \"" << serial_number << "\",\n"
             << "  \"uuid\": \"" << uuid << "\",\n"
             << "  \"materialNumber\": \"11148775\",\n"
             << "  \"productName\": \"ACUSON Sequoia\",\n"
             << "  \"connectionStatus\": 1,\n"
             << "  \"status\": 1,\n"
             << "  \"isFullAccessAllowed\": true\n"
             << "}";
        
        // Build HTTPS request
        HttpsRequest request;
        request.url = comm_info_url;
        request.method = "GET";
        request.body = body.str();
        request.timeout_ms = 30000;
        
        // Set headers (same as auth_manager)
        request.headers["Content-Type"] = "application/json";
        request.headers["Accept"] = "*/*";
        request.headers["ARS-ClientCert"] = cert_content;
        request.headers["User-Agent"] = "AgentCore/0.1.0";
        
        // Create retry policy from config
        auto retry_policy = create_retry_policy(config.retry);
        
        std::cout << "  - Sending communication info request (max attempts: "
                  << config.retry.max_attempts << ")...\n";
        
        bool success = false;
        std::string response_body;
        
        // Use retry policy for fetching comm info
        success = retry_policy->execute([&]() {
            HttpsResponse response = https_client_->send(request);
            
            // Check for network errors
            if (!response.error.empty()) {
                std::cerr << "CommInfoManager: Network error: " << response.error << " - retrying...\n";
                return false;
            }
            
            std::cout << "  - Response status code: " << response.status_code << "\n";
            
            // Success case
            if (response.status_code == 200) {
                response_body = response.body;
                std::cout << "  - Response body: " << response_body << "\n";
                return true;
            }
            
            // Transient errors (5xx) should be retried
            if (response.status_code >= 500 && response.status_code < 600) {
                std::cerr << "CommInfoManager: Server error (" << response.status_code << ") - retrying...\n";
                return false;
            }
            
            // Client errors (4xx) should not be retried
            if (response.status_code >= 400 && response.status_code < 500) {
                std::cerr << "CommInfoManager: Client error (" << response.status_code << ") - not retrying\n";
                std::cerr << "  - Response body: " << response.body << "\n";
                retry_policy->reset();
                return false;
            }
            
            // Other errors
            std::cerr << "CommInfoManager: Unexpected status (" << response.status_code << ") - retrying...\n";
            return false;
        });
        
        if (!success) {
            std::cerr << "CommInfoManager: ERROR - Failed to fetch communication info after all retry attempts\n";
            return false;
        }
        
        // Parse JSON response
        try {
            json j = json::parse(response_body);
            
            if (j.contains("region") && j["region"].is_string()) {
                comm_info.region = j["region"].get<std::string>();
            }
            if (j.contains("endPoint") && j["endPoint"].is_string()) {
                comm_info.endpoint = j["endPoint"].get<std::string>();
            }
            if (j.contains("accessKey") && j["accessKey"].is_string()) {
                comm_info.access_key = j["accessKey"].get<std::string>();
            }
            if (j.contains("secretKey") && j["secretKey"].is_string()) {
                comm_info.secret_key = j["secretKey"].get<std::string>();
            }
            if (j.contains("token") && j["token"].is_string()) {
                comm_info.token = j["token"].get<std::string>();
            }
            
            if (!comm_info.is_valid()) {
                std::cerr << "CommInfoManager: ERROR - Invalid communication info received\n";
                std::cerr << "  - Endpoint: " << (comm_info.endpoint.empty() ? "MISSING" : "OK") << "\n";
                std::cerr << "  - Access Key: " << (comm_info.access_key.empty() ? "MISSING" : "OK") << "\n";
                std::cerr << "  - Secret Key: " << (comm_info.secret_key.empty() ? "MISSING" : "OK") << "\n";
                std::cerr << "  - Token: " << (comm_info.token.empty() ? "MISSING" : "OK") << "\n";
                return false;
            }
            
            std::cout << "CommInfoManager: ✓ Communication info fetched successfully\n";
            std::cout << "  - Region: " << comm_info.region << "\n";
            std::cout << "  - Endpoint: " << comm_info.endpoint << "\n";
            std::cout << "  - Access Key: " << comm_info.access_key.substr(0, 4) << "..." << "\n";
            
            return true;
            
        } catch (const json::exception& e) {
            std::cerr << "CommInfoManager: ERROR - Failed to parse JSON response: " << e.what() << "\n";
            return false;
        }
    }

private:
    std::unique_ptr<HttpsClient> https_client_;
    
    std::string read_certificate(const std::string& cert_path) {
        std::ifstream file(cert_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open certificate file: " << cert_path << "\n";
            return "";
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        // Trim whitespace
        content.erase(0, content.find_first_not_of(" \t\r\n"));
        content.erase(content.find_last_not_of(" \t\r\n") + 1);
        
        return content;
    }
};

std::unique_ptr<CommInfoManager> create_comm_info_manager() {
    return std::make_unique<CommInfoManagerImpl>();
}

}
