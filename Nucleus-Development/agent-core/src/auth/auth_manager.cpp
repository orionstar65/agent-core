#include "agent/auth_manager.hpp"
#include "agent/https_client.hpp"
#include "agent/retry.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

namespace agent {

class AuthManagerImpl : public AuthManager {
public:
    AuthManagerImpl() : https_client_(create_https_client()) {}
    
    CertState ensure_certificate(const Identity& identity, const Config& config) override {
        std::cout << "AuthManager: Starting authentication for "
                  << (identity.is_gateway ? "gateway " + identity.gateway_id
                                          : "device " + identity.device_serial)
                  << "\n";
        
        // Get serial number and UUID from identity
        std::string serial_number = identity.device_serial;
        std::string uuid = identity.uuid;
        
        if (serial_number.empty()) {
            std::cerr << "AuthManager: ERROR - Device serial number is empty\n";
            return CertState::Failed;
        }
        
        if (uuid.empty()) {
            std::cerr << "AuthManager: ERROR - UUID is empty\n";
            return CertState::Failed;
        }
        
        // Read certificate from file
        std::string cert_content = read_certificate(config.cert.cert_path);
        if (cert_content.empty()) {
            std::cerr << "AuthManager: ERROR - Failed to read certificate from: "
                      << config.cert.cert_path << "\n";
            return CertState::Failed;
        }
        
        std::cout << "  - Serial Number: " << serial_number << "\n";
        std::cout << "  - UUID: " << uuid << "\n";
        std::cout << "  - Certificate loaded from: " << config.cert.cert_path << "\n";
        std::cout << "  - Backend URL: " << config.backend.base_url << "\n";
        
        // Build authentication URL
        std::string auth_url = config.backend.base_url + 
                               config.backend.auth_path +
                               serial_number + "/" + uuid;
        
        std::cout << "  - Authentication URL: " << auth_url << "\n";
        
        // Build request body
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
        request.url = auth_url;
        request.method = "GET";
        request.body = body.str();
        request.timeout_ms = 30000;
        
        // Set headers
        request.headers["Content-Type"] = "application/json";
        request.headers["Accept"] = "*/*";
        request.headers["ARS-ClientCert"] = cert_content;
        request.headers["User-Agent"] = "AgentCore/0.1.0";
        
        // Create retry policy from config
        auto retry_policy = create_retry_policy(config.retry);
        


        std::cout << "  - Sending authentication request (max attempts: "
                  << config.retry.max_attempts << ")...\n";

        // Use retry policy for authentication
        bool success = retry_policy->execute([&]() {
            HttpsResponse response = https_client_->send(request);
            
            // Check for network errors
            if (!response.error.empty()) {
                std::cerr << "AuthManager: Network error: " << response.error << " - retrying...\n";
                return false;
            }
            
            std::cout << "  - Response status code: " << response.status_code << "\n";
            
            // Success case
            if (response.status_code == 200) {
                std::cout << "  - Response body: " << response.body << "\n";
                std::cout << "AuthManager: ✓ Authentication successful\n";
                return true;
            }
            
            // Transient errors (5xx) should be retried
            if (response.status_code >= 500 && response.status_code < 600) {
                std::cerr << "AuthManager: Server error (" << response.status_code << ") - retrying...\n";
                return false;
            }
            
            // Client errors (4xx) should not be retried
            if (response.status_code >= 400 && response.status_code < 500) {
                std::cerr << "AuthManager: Client error (" << response.status_code << ") - not retrying\n";
                std::cerr << "  - Response body: " << response.body << "\n";
                // Stop retrying for client errors by resetting the policy
                retry_policy->reset();
                return false;
            }
            
            // Other errors
            std::cerr << "AuthManager: Unexpected status (" << response.status_code << ") - retrying...\n";
            return false;
        });

        if (success) {
            return CertState::Valid;
        } else {


            std::cerr << "AuthManager: ERROR - Authentication failed after all retry attempts\n";
            return CertState::Failed;
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

std::unique_ptr<AuthManager> create_auth_manager() {
    return std::make_unique<AuthManagerImpl>();
}

}
