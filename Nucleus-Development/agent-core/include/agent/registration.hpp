#pragma once

#include <memory>
#include "config.hpp"
#include "identity.hpp"

namespace agent {

enum class RegistrationState {
    NotRegistered,
    Registered,
    Failed
};

class Registration {
public:
    virtual ~Registration() = default;
    
    // Register device/gateway with backend (For MVP SSM then we can change later)
    virtual RegistrationState register_device(const Identity& identity, const Config& config) = 0;
};

// Create SSM-based registration implementation (MVP)
std::unique_ptr<Registration> create_ssm_registration();

}
