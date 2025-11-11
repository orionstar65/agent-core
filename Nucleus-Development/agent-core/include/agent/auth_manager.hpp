#pragma once

#include <memory>
#include "config.hpp"
#include "identity.hpp"

namespace agent {

enum class CertState {
    Valid,
    Renewed,
    Failed
};

class AuthManager {
public:
    virtual ~AuthManager() = default;
    
    // Ensure certificate is valid, renew if needed
    virtual CertState ensure_certificate(const Identity& identity, const Config& config) = 0;
};

std::unique_ptr<AuthManager> create_auth_manager();

}
