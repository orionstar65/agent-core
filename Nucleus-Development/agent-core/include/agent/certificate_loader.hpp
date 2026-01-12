#pragma once

#include "config.hpp"
#include <string>

namespace agent {

struct CertificateLoadResult {
    std::string certificate_content;
    std::string source_description;
    bool success{false};
    std::string error_message;
};

CertificateLoadResult load_certificate(const Config& config);

std::string resolve_certificate_path(const Config& config);

bool validate_certificate_configuration(const Config& config, std::string& error_message);

}
