#include "agent/cert_validator.hpp"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/x509v3.h>
#include <ctime>
#include <sstream>
#include <cstring>

namespace agent {

CertValidationResult validate_certificate(const std::string& cert_content) {
    if (cert_content.empty()) {
        return CertValidationResult(false, CertValidationError::InvalidFormat, "Certificate content is empty");
    }
    
    BIO* bio = BIO_new_mem_buf(cert_content.c_str(), static_cast<int>(cert_content.length()));
    if (!bio) {
        return CertValidationResult(false, CertValidationError::InvalidFormat, "Failed to create BIO buffer");
    }
    
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    
    if (!cert) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        std::string error_msg = "Failed to parse certificate: ";
        error_msg += err_buf;
        return CertValidationResult(false, CertValidationError::InvalidFormat, error_msg);
    }
    
    time_t current_time = std::time(nullptr);
    if (current_time == -1) {
        X509_free(cert);
        return CertValidationResult(false, CertValidationError::InvalidFormat, "Failed to get current time");
    }
    
    ASN1_TIME* not_before = X509_get_notBefore(cert);
    ASN1_TIME* not_after = X509_get_notAfter(cert);
    
    if (!not_before || !not_after) {
        X509_free(cert);
        return CertValidationResult(false, CertValidationError::InvalidFormat, "Certificate missing validity dates");
    }
    
    int cmp_before = X509_cmp_time(not_before, &current_time);
    if (cmp_before == -2) {
        X509_free(cert);
        return CertValidationResult(false, CertValidationError::InvalidFormat, "Failed to compare certificate notBefore date");
    }
    if (cmp_before == 1) {
        X509_free(cert);
        return CertValidationResult(false, CertValidationError::NotYetValid, "Certificate is not yet valid");
    }
    
    int cmp_after = X509_cmp_time(not_after, &current_time);
    if (cmp_after == -2) {
        X509_free(cert);
        return CertValidationResult(false, CertValidationError::InvalidFormat, "Failed to compare certificate notAfter date");
    }
    if (cmp_after == -1) {
        X509_free(cert);
        return CertValidationResult(false, CertValidationError::Expired, "Certificate has expired");
    }
    
    X509_free(cert);
    return CertValidationResult(true, CertValidationError::None, "");
}

}

