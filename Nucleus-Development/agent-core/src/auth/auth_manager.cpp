#include "agent/auth_manager.hpp"
#include <iostream>

namespace agent {

class AuthManagerImpl : public AuthManager {
public:
    CertState ensure_certificate(const Identity& identity, const Config& config) override {
        // TODO: Implement auth workflow
        std::cout << "AuthManager: Checking certificate for "
                  << (identity.is_gateway ? "gateway " + identity.gateway_id
                                          : "device " + identity.device_serial)
                  << "\n";
        
        std::cout << "  - Store hint: " << config.cert.store_hint << "\n";
        
        // For now, always return Valid
        return CertState::Valid;
    }
};

std::unique_ptr<AuthManager> create_auth_manager() {
    return std::make_unique<AuthManagerImpl>();
}

}
