#pragma once

#include <memory>
#include <string>
#include "config.hpp"
#include "identity.hpp"

namespace agent {

enum class RegistrationState {
    NotRegistered,
    Registered,
    Failed
};

struct ActivationInfo {
    std::string activation_id;
    std::string activation_code;
    std::string region;
};

class Registration {
public:
    virtual ~Registration() = default;
    
    /// Check if device is registered with backend
    virtual bool is_device_registered(const Identity& identity, const Config& config) = 0;
    
    /// Check if device is registered locally (SSM agent installed and configured)
    virtual bool is_locally_registered() = 0;
    
    /// Get activation information from backend
    virtual bool get_activation_info(const Identity& identity, const Config& config, ActivationInfo& info) = 0;
    
    /// Register device with SSM using activation info
    virtual RegistrationState register_with_ssm(const ActivationInfo& info) = 0;
    
    /// Full registration flow: check status, get activation if needed, register
    virtual RegistrationState register_device(const Identity& identity, const Config& config) = 0;
};

/// Create SSM-based registration implementation
std::unique_ptr<Registration> create_ssm_registration();

}
