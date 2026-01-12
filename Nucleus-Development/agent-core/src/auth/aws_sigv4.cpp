#include "agent/aws_sigv4.hpp"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cctype>

namespace agent {

namespace {

// URL encode a string (RFC 3986)
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            // Any other characters are percent-encoded
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

// Convert bytes to hex string
std::string to_hex(const unsigned char* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

// SHA256 hash
std::string sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
    return to_hex(hash, SHA256_DIGEST_LENGTH);
}

// HMAC-SHA256
std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    HMAC(EVP_sha256(),
         key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    return std::string(reinterpret_cast<char*>(hash), hash_len);
}

// Get signing key for AWS SigV4
std::string get_signing_key(const std::string& secret_key,
                           const std::string& date_stamp,
                           const std::string& region,
                           const std::string& service) {
    std::string k_date = hmac_sha256("AWS4" + secret_key, date_stamp);
    std::string k_region = hmac_sha256(k_date, region);
    std::string k_service = hmac_sha256(k_region, service);
    std::string k_signing = hmac_sha256(k_service, "aws4_request");
    return k_signing;
}

// Get current UTC time
void get_utc_time(std::string& amz_date, std::string& date_stamp) {
    time_t now = time(nullptr);
    struct tm* tm_info = gmtime(&now);
    
    char date_buf[20];
    char stamp_buf[10];
    
    strftime(date_buf, sizeof(date_buf), "%Y%m%dT%H%M%SZ", tm_info);
    strftime(stamp_buf, sizeof(stamp_buf), "%Y%m%d", tm_info);
    
    amz_date = date_buf;
    date_stamp = stamp_buf;
}

} // anonymous namespace

std::string sign_aws_iot_websocket_url(
    const std::string& endpoint,
    const std::string& region,
    const AwsCredentials& credentials) {
    
    const std::string service = "iotdata";
    const std::string method = "GET";
    const std::string canonical_uri = "/mqtt";
    const std::string algorithm = "AWS4-HMAC-SHA256";
    
    // Get current time
    std::string amz_date, date_stamp;
    get_utc_time(amz_date, date_stamp);
    
    // Credential scope
    std::string credential_scope = date_stamp + "/" + region + "/" + service + "/aws4_request";
    
    // Canonical headers
    std::string host = endpoint;
    std::string canonical_headers = "host:" + host + "\n";
    std::string signed_headers = "host";
    
    // Canonical query string for signing (WITHOUT security token - it's added later)
    // Per AWS IoT SigV4 WebSocket spec, the token is NOT included when computing the signature
    std::string canonical_querystring = "X-Amz-Algorithm=" + algorithm;
    canonical_querystring += "&X-Amz-Credential=" + url_encode(credentials.access_key + "/" + credential_scope);
    canonical_querystring += "&X-Amz-Date=" + amz_date;
    canonical_querystring += "&X-Amz-SignedHeaders=" + signed_headers;
    
    // Payload hash (empty for WebSocket connection)
    std::string payload_hash = sha256("");
    
    // Create canonical request
    std::string canonical_request = method + "\n" +
                                   canonical_uri + "\n" +
                                   canonical_querystring + "\n" +
                                   canonical_headers + "\n" +
                                   signed_headers + "\n" +
                                   payload_hash;
    
    // Create string to sign
    std::string string_to_sign = algorithm + "\n" +
                                 amz_date + "\n" +
                                 credential_scope + "\n" +
                                 sha256(canonical_request);
    
    // Calculate signature
    std::string signing_key = get_signing_key(credentials.secret_key, date_stamp, region, service);
    std::string signature = to_hex(
        reinterpret_cast<const unsigned char*>(hmac_sha256(signing_key, string_to_sign).c_str()),
        SHA256_DIGEST_LENGTH);
    
    // Build final query string: add security token (if present), then signature
    // Order: Algorithm, Credential, Date, SignedHeaders, Security-Token, Signature
    std::string final_querystring = canonical_querystring;
    if (!credentials.session_token.empty()) {
        final_querystring += "&X-Amz-Security-Token=" + url_encode(credentials.session_token);
    }
    final_querystring += "&X-Amz-Signature=" + signature;
    
    // Build final URL (no port number - 443 is implicit for wss://)
    std::string signed_url = "wss://" + host + canonical_uri + "?" + final_querystring;
    
    return signed_url;
}

}
