#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <exception>

namespace agent {
namespace envelope_json {

struct Envelope {
    std::string topic;
    std::string correlation_id;
    std::string payload_json;
    int64_t ts_ms{0};
};

inline std::string escape_json_string(const std::string& str) {
    std::ostringstream o;
    for (size_t i = 0; i < str.length(); ++i) {
        switch (str[i]) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(str[i]) < 0x20) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') 
                      << static_cast<int>(static_cast<unsigned char>(str[i]));
                } else {
                    o << str[i];
                }
        }
    }
    return o.str();
}

inline std::string extract_json_string(const std::string& json, const std::string& key, size_t& pos) {
    std::string key_pattern = "\"" + key + "\":\"";
    size_t key_pos = json.find(key_pattern, pos);
    if (key_pos == std::string::npos) {
        return "";
    }
    
    size_t start = key_pos + key_pattern.length();
    size_t end = start;
    bool escaped = false;
    
    while (end < json.length()) {
        if (escaped) {
            escaped = false;
            if (json[end] == 'u' && end + 4 < json.length()) {
                end += 4;
            }
        } else if (json[end] == '\\') {
            escaped = true;
        } else if (json[end] == '"') {
            break;
        }
        ++end;
    }
    
    pos = end + 1;
    std::string result = json.substr(start, end - start);
    
    std::ostringstream unescaped;
    for (size_t i = 0; i < result.length(); ++i) {
        if (result[i] == '\\' && i + 1 < result.length()) {
            switch (result[++i]) {
                case '"': unescaped << '"'; break;
                case '\\': unescaped << '\\'; break;
                case 'b': unescaped << '\b'; break;
                case 'f': unescaped << '\f'; break;
                case 'n': unescaped << '\n'; break;
                case 'r': unescaped << '\r'; break;
                case 't': unescaped << '\t'; break;
                case 'u':
                    if (i + 4 < result.length()) {
                        try {
                            std::string hex = result.substr(i + 1, 4);
                            unescaped << static_cast<char>(std::stoi(hex, nullptr, 16));
                            i += 4;
                        } catch (const std::exception&) {
                            // Invalid hex sequence, treat as literal
                            unescaped << result[i];
                        }
                    } else {
                        unescaped << result[i];
                    }
                    break;
                default: unescaped << result[i]; break;
            }
        } else {
            unescaped << result[i];
        }
    }
    return unescaped.str();
}

inline int64_t extract_json_int64(const std::string& json, const std::string& key, size_t& pos) {
    std::string key_pattern = "\"" + key + "\":";
    size_t key_pos = json.find(key_pattern, pos);
    if (key_pos == std::string::npos) {
        return 0;
    }
    
    size_t start = key_pos + key_pattern.length();
    while (start < json.length() && std::isspace(json[start])) {
        ++start;
    }
    
    size_t end = start;
    while (end < json.length() && (std::isdigit(json[end]) || json[end] == '-')) {
        ++end;
    }
    
    pos = end;
    std::string num_str = json.substr(start, end - start);
    return std::stoll(num_str);
}

inline std::string extract_json_value(const std::string& json, const std::string& key, size_t& pos) {
    std::string key_pattern = "\"" + key + "\":";
    size_t key_pos = json.find(key_pattern, pos);
    if (key_pos == std::string::npos) {
        return "";
    }
    
    size_t start = key_pos + key_pattern.length();
    while (start < json.length() && std::isspace(json[start])) {
        ++start;
    }
    
    if (start >= json.length()) {
        return "";
    }
    
    if (json[start] == '"') {
        return extract_json_string(json, key, pos);
    }
    
    if (json[start] == '{' || json[start] == '[') {
        int depth = 0;
        size_t end = start;
        do {
            if (json[end] == '{' || json[end] == '[') ++depth;
            if (json[end] == '}' || json[end] == ']') --depth;
            ++end;
        } while (end < json.length() && depth > 0);
        pos = end;
        return json.substr(start, end - start);
    }
    
    size_t end = start;
    while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        ++end;
    }
    pos = end;
    std::string result = json.substr(start, end - start);
    while (!result.empty() && std::isspace(result.back())) {
        result.pop_back();
    }
    return result;
}

template<typename EnvType>
inline std::string serialize_envelope_template(const EnvType& envelope) {
    std::ostringstream json;
    json << "{"
         << "\"v\":1,"
         << "\"topic\":\"" << escape_json_string(envelope.topic) << "\","
         << "\"correlationId\":\"" << escape_json_string(envelope.correlation_id) << "\","
         << "\"payload\":" << envelope.payload_json << ","
         << "\"ts\":" << envelope.ts_ms
         << "}";
    return json.str();
}

template<typename EnvType>
inline bool deserialize_envelope_template(const std::string& json_str, EnvType& envelope) {
    try {
        size_t pos = 0;
        
        // Check if "topic" key exists in JSON
        std::string key_pattern = "\"topic\":";
        size_t topic_key_pos = json_str.find(key_pattern, pos);
        if (topic_key_pos == std::string::npos) {
            return false; // Key not found
        }
        
        std::string topic = extract_json_string(json_str, "topic", pos);
        envelope.topic = topic; // Allow empty topic if key exists
        
        std::string correlation_id = extract_json_string(json_str, "correlationId", pos);
        envelope.correlation_id = correlation_id;
        
        std::string payload = extract_json_value(json_str, "payload", pos);
        envelope.payload_json = payload;
        
        int64_t ts = extract_json_int64(json_str, "ts", pos);
        envelope.ts_ms = ts;
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

inline std::string serialize_envelope(const Envelope& envelope) {
    return serialize_envelope_template(envelope);
}

inline bool deserialize_envelope(const std::string& json_str, Envelope& envelope) {
    return deserialize_envelope_template(json_str, envelope);
}

}
}
