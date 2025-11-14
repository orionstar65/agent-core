#include "agent/registration.hpp"
#include <iostream>

// TODO: Add AWS SDK includes for SSM
// #include <aws/ssm/SSMClient.h>

namespace agent {

class SsmRegistrationImpl : public Registration {
public:
    RegistrationState register_device(const Identity& identity, const Config& config) override {
        std::cout << "Registration (SSM MVP): Registering "
                  << (identity.is_gateway ? "gateway " + identity.gateway_id
                                          : "device " + identity.device_serial)
                  << "\n";
        std::cout << "  - Backend: " << config.backend.base_url << "\n";
        std::cout << "  - TODO: Implement actual SSM registration\n";
        
        // For now, always succeed
        return RegistrationState::Registered;
    }
};

std::unique_ptr<Registration> create_ssm_registration() {
    return std::make_unique<SsmRegistrationImpl>();
}

}
