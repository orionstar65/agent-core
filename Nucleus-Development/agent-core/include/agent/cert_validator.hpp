#pragma once

#include <string>

namespace agent {

enum class CertValidationError {
    None,
    Expired,
    InvalidFormat,
    NotYetValid
};

struct CertValidationResult {
    bool valid;
    std::string error_message;
    CertValidationError error;
    
    CertValidationResult() : valid(false), error_message(""), error(CertValidationError::None) {}
    CertValidationResult(bool is_valid, CertValidationError err = CertValidationError::None, const std::string& msg = "")
        : valid(is_valid), error_message(msg), error(err) {}
};

CertValidationResult validate_certificate(const std::string& cert_content);

}

