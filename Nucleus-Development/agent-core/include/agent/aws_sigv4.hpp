#pragma once

#include <string>

namespace agent {

// AWS SigV4 URL signing for MQTT WebSocket connections
// This generates a pre-signed URL that can be used to connect to AWS IoT Core
// via WebSocket without additional authentication headers

struct AwsCredentials {
    std::string access_key;
    std::string secret_key;
    std::string session_token;
    std::string region;
};

// Generate a SigV4 signed WebSocket URL for AWS IoT Core
// endpoint: AWS IoT endpoint (e.g., "xxx.iot.region.amazonaws.com")
// region: AWS region (e.g., "eu-central-1")
// credentials: AWS credentials (access_key, secret_key, session_token)
// Returns: Signed WebSocket URL (wss://...)
std::string sign_aws_iot_websocket_url(
    const std::string& endpoint,
    const std::string& region,
    const AwsCredentials& credentials);

}
