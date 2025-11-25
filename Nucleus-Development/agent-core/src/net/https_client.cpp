#include "agent/https_client.hpp"
#include <iostream>
#include <curl/curl.h>
#include <sstream>

namespace agent {

// Callback function for libcurl to write response data
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Callback function for libcurl to write headers
static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total_size = size * nitems;
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    
    std::string header(buffer, total_size);
    size_t colon_pos = header.find(':');
    if (colon_pos != std::string::npos) {
        std::string key = header.substr(0, colon_pos);
        std::string value = header.substr(colon_pos + 1);
        
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        
        (*headers)[key] = value;
    }
    
    return total_size;
}

class HttpsClientImpl : public HttpsClient {
public:
    HttpsClientImpl() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    ~HttpsClientImpl() override {
        curl_global_cleanup();
    }
    
    HttpsResponse send(const HttpsRequest& request) override {
        HttpsResponse response;
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            response.error = "Failed to initialize CURL";
            return response;
        }
        
        std::string response_body;
        std::map<std::string, std::string> response_headers;
        
        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        
        // Set method
        if (request.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
        } else if (request.method == "GET") {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            if (!request.body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
            }
        }
        
        // Set headers
        struct curl_slist* headers_list = nullptr;
        for (const auto& [key, value] : request.headers) {
            std::string header = key + ": " + value;
            headers_list = curl_slist_append(headers_list, header.c_str());
        }
        if (headers_list) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);
        }
        
        // Set callbacks
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
        
        // TLS/SSL options
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Disable for self-signed certs
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // Disable hostname verification
        
        // Timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)request.timeout_ms);
        
        // Enable verbose logging for debugging
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            response.error = curl_easy_strerror(res);
            std::cerr << "HTTPS request failed: " << response.error << "\n";
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);
            response.body = response_body;
            response.headers = response_headers;
        }
        
        // Cleanup
        if (headers_list) {
            curl_slist_free_all(headers_list);
        }
        curl_easy_cleanup(curl);
        
        return response;
    }
};

std::unique_ptr<HttpsClient> create_https_client() {
    return std::make_unique<HttpsClientImpl>();
}

}
