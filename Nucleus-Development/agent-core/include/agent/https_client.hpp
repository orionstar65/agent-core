#pragma once

#include <string>
#include <map>
#include <memory>

namespace agent {

struct HttpsRequest {
    std::string url;
    std::string method{"POST"};
    std::map<std::string, std::string> headers;
    std::string body;
    int timeout_ms{30000};
};

struct HttpsResponse {
    int status_code{0};
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;
};

class HttpsClient {
public:
    virtual ~HttpsClient() = default;
    
    /// Send HTTPS request with TLS verification
    virtual HttpsResponse send(const HttpsRequest& request) = 0;
};

/// Create HTTPS client implementation
std::unique_ptr<HttpsClient> create_https_client();

}
