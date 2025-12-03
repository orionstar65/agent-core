#include "agent/registration.hpp"
#include "agent/https_client.hpp"
#include "agent/retry.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace agent {

class SsmRegistrationImpl : public Registration {
public:
    SsmRegistrationImpl() : https_client_(create_https_client()) {}
    
    bool is_device_registered(const Identity& identity, const Config& config) override {
        std::cout << "Registration: Checking if device is registered with backend\n";
        
        std::string serial = identity.device_serial;
        std::string uuid = identity.uuid;
        
        std::string url = config.backend.base_url + 
                          config.backend.is_registered_path +
                          serial + "/" + uuid;
        
        std::cout << "  - URL: " << url << "\n";
        
        std::string cert_content = read_certificate(config.cert.cert_path);
        if (cert_content.empty()) {
            std::cerr << "Registration: ERROR - Failed to read certificate\n";
            return false;
        }
        
        HttpsRequest request;
        request.url = url;
        request.method = "GET";
        request.timeout_ms = 30000;
        request.headers["Content-Type"] = "application/json";
        request.headers["Accept"] = "*/*";
        request.headers["ARS-ClientCert"] = cert_content;
        request.headers["User-Agent"] = "AgentCore/0.1.0";
        
        auto retry_policy = create_retry_policy(config.retry);
        bool api_success = false;
        std::string response_body;
        
        retry_policy->execute([&]() {
            HttpsResponse response = https_client_->send(request);
            
            if (!response.error.empty()) {
                std::cerr << "Registration: Network error: " << response.error << " - retrying...\n";
                return false;
            }
            
            if (response.status_code == 200) {
                response_body = response.body;
                api_success = true;
                return true;
            }
            
            if (response.status_code >= 500) {
                std::cerr << "Registration: Server error (" << response.status_code << ") - retrying...\n";
                return false;
            }
            
            std::cerr << "Registration: Client error (" << response.status_code << ")\n";
            retry_policy->reset();
            return false;
        });
        
        if (!api_success) {
            std::cerr << "Registration: Failed to check registration status\n";
            return false;
        }
        
        std::string trimmed = response_body;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n\""));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n\"") + 1);
        
        bool is_registered = (trimmed == "true" || trimmed == "True" || trimmed == "TRUE");
        std::cout << "  - Backend says device is registered: " << (is_registered ? "yes" : "no") << "\n";
        
        return is_registered;
    }
    
    bool is_locally_registered() override {
        std::cout << "Registration: Checking local SSM registration status\n";
        
#ifdef _WIN32
        SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) {
            std::cout << "  - Cannot open SCM\n";
            return false;
        }
        
        SC_HANDLE service = OpenService(scm, "AmazonSSMAgent", SERVICE_QUERY_STATUS);
        if (!service) {
            CloseServiceHandle(scm);
            std::cout << "  - SSM Agent service not found\n";
            return false;
        }
        
        SERVICE_STATUS status;
        bool running = QueryServiceStatus(service, &status) && 
                       status.dwCurrentState == SERVICE_RUNNING;
        
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        
        std::cout << "  - SSM Agent service " << (running ? "is running" : "is not running") << "\n";
        return running;
#else
        int result = system("systemctl is-active --quiet amazon-ssm-agent 2>/dev/null");
        bool running = (result == 0);
        
        if (!running) {
            result = system("systemctl is-active --quiet snap.amazon-ssm-agent.amazon-ssm-agent 2>/dev/null");
            running = (result == 0);
        }
        
        std::cout << "  - SSM Agent " << (running ? "is running" : "is not running") << "\n";
        return running;
#endif
    }
    
    bool get_activation_info(const Identity& identity, const Config& config, ActivationInfo& info) override {
        std::cout << "Registration: Getting activation information from backend\n";
        
        std::string serial = identity.device_serial;
        std::string uuid = identity.uuid;
        
        std::string url = config.backend.base_url + 
                          config.backend.get_activation_path +
                          serial + "/" + uuid;
        
        std::cout << "  - URL: " << url << "\n";
        
        std::string cert_content = read_certificate(config.cert.cert_path);
        if (cert_content.empty()) {
            std::cerr << "Registration: ERROR - Failed to read certificate\n";
            return false;
        }
        
        HttpsRequest request;
        request.url = url;
        request.method = "GET";
        request.timeout_ms = 30000;
        request.headers["Content-Type"] = "application/json";
        request.headers["Accept"] = "*/*";
        request.headers["ARS-ClientCert"] = cert_content;
        request.headers["User-Agent"] = "AgentCore/0.1.0";
        
        auto retry_policy = create_retry_policy(config.retry);
        bool api_success = false;
        std::string response_body;
        
        retry_policy->execute([&]() {
            HttpsResponse response = https_client_->send(request);
            
            if (!response.error.empty()) {
                std::cerr << "Registration: Network error: " << response.error << " - retrying...\n";
                return false;
            }
            
            if (response.status_code == 200) {
                response_body = response.body;
                api_success = true;
                return true;
            }
            
            if (response.status_code >= 500) {
                std::cerr << "Registration: Server error (" << response.status_code << ") - retrying...\n";
                return false;
            }
            
            std::cerr << "Registration: Client error (" << response.status_code << ")\n";
            retry_policy->reset();
            return false;
        });
        
        if (!api_success) {
            std::cerr << "Registration: Failed to get activation information\n";
            return false;
        }
        
        if (!parse_activation_info(response_body, info)) {
            std::cerr << "Registration: Failed to parse activation info\n";
            return false;
        }
        
        std::cout << "  - ActivationID: " << info.activation_id << "\n";
        std::cout << "  - Region: " << info.region << "\n";
        std::cout << "  - ActivationCode: [REDACTED]\n";
        
        return true;
    }
    
    RegistrationState register_with_ssm(const ActivationInfo& info) override {
        std::cout << "Registration: Registering with SSM Agent\n";
        
        if (info.activation_id.empty() || info.activation_code.empty() || info.region.empty()) {
            std::cerr << "Registration: ERROR - Invalid activation info\n";
            return RegistrationState::Failed;
        }
        
#ifdef _WIN32
        std::string cmd = "\"" + ssm_agent_path_ + "\" -register ";
        cmd += "-code \"" + info.activation_code + "\" ";
        cmd += "-id \"" + info.activation_id + "\" ";
        cmd += "-region \"" + info.region + "\"";
        
        std::cout << "  - Executing: amazon-ssm-agent.exe -register -code [REDACTED] -id " 
                  << info.activation_id << " -region " << info.region << "\n";
        
        int result = system(cmd.c_str());
        if (result != 0) {
            std::cerr << "Registration: SSM registration command failed with code: " << result << "\n";
            return RegistrationState::Failed;
        }
#else
        std::string agent_path = ssm_agent_path_.empty() ? "/usr/bin/amazon-ssm-agent" : ssm_agent_path_;
        
        std::string cmd = agent_path + " -register ";
        cmd += "-code '" + info.activation_code + "' ";
        cmd += "-id '" + info.activation_id + "' ";
        cmd += "-region '" + info.region + "' ";
        cmd += "-y";
        
        std::cout << "  - Executing: amazon-ssm-agent -register -code [REDACTED] -id " 
                  << info.activation_id << " -region " << info.region << " -y\n";
        
        int result = system(cmd.c_str());
        if (result != 0) {
            std::cerr << "Registration: SSM registration command failed with code: " << result << "\n";
            return RegistrationState::Failed;
        }
        
        std::cout << "  - Starting SSM Agent service\n";
        int start_result = system("sudo systemctl restart amazon-ssm-agent 2>/dev/null || "
                                   "sudo systemctl restart snap.amazon-ssm-agent.amazon-ssm-agent 2>/dev/null");
        if (start_result != 0) {
            std::cerr << "Registration: Warning - Could not start SSM agent service\n";
        }
#endif
        
        std::cout << "Registration: SSM registration successful\n";
        return RegistrationState::Registered;
    }
    
    RegistrationState register_device(const Identity& identity, const Config& config) override {
        std::cout << "Registration: Starting registration flow for "
                  << (identity.is_gateway ? "gateway " + identity.gateway_id
                                          : "device " + identity.device_serial)
                  << "\n";
        
        ssm_agent_path_ = config.ssm.agent_path;
        
        bool backend_registered = is_device_registered(identity, config);
        bool local_registered = is_locally_registered();
        
        if (backend_registered && local_registered) {
            std::cout << "Registration: Device is already registered (backend and local)\n";
            return RegistrationState::Registered;
        }
        
        std::cout << "Registration: Device needs registration\n";
        std::cout << "  - Backend registered: " << (backend_registered ? "yes" : "no") << "\n";
        std::cout << "  - Locally registered: " << (local_registered ? "yes" : "no") << "\n";
        
        ActivationInfo activation_info;
        if (!get_activation_info(identity, config, activation_info)) {
            std::cerr << "Registration: Failed to get activation information\n";
            return RegistrationState::Failed;
        }
        
        return register_with_ssm(activation_info);
    }

private:
    std::unique_ptr<HttpsClient> https_client_;
    std::string ssm_agent_path_;
    
    std::string read_certificate(const std::string& cert_path) {
        std::ifstream file(cert_path);
        if (!file.is_open()) {
            return "";
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        content.erase(0, content.find_first_not_of(" \t\r\n"));
        content.erase(content.find_last_not_of(" \t\r\n") + 1);
        
        return content;
    }
    
    bool parse_activation_info(const std::string& json_str, ActivationInfo& info) {
        auto extract_value = [](const std::string& json, const std::string& key) -> std::string {
            std::string search_key = "\"" + key + "\"";
            size_t key_pos = json.find(search_key);
            if (key_pos == std::string::npos) {
                std::string lower_key = key;
                for (char& c : lower_key) c = std::tolower(c);
                search_key = "\"" + lower_key + "\"";
                key_pos = json.find(search_key);
                if (key_pos == std::string::npos) return "";
            }
            
            size_t colon_pos = json.find(':', key_pos);
            if (colon_pos == std::string::npos) return "";
            
            size_t quote_start = json.find('"', colon_pos);
            if (quote_start == std::string::npos) return "";
            
            size_t quote_end = json.find('"', quote_start + 1);
            if (quote_end == std::string::npos) return "";
            
            return json.substr(quote_start + 1, quote_end - quote_start - 1);
        };
        
        info.activation_id = extract_value(json_str, "activationId");
        info.activation_code = extract_value(json_str, "activationCode");
        info.region = extract_value(json_str, "region");
        
        return !info.activation_id.empty() && !info.activation_code.empty() && !info.region.empty();
    }
};

std::unique_ptr<Registration> create_ssm_registration() {
    return std::make_unique<SsmRegistrationImpl>();
}

}
