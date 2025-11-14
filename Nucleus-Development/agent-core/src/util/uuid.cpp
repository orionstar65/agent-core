#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace agent {
namespace util {

std::string generate_uuid() {
    // Simple UUID v4 generator (not cryptographically secure)
    // TODO: 
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    ss << std::hex;
    
    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4"; // UUID version 4
    
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    ss << dis2(gen); // UUID variant
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    
    return ss.str();
}

}
}
