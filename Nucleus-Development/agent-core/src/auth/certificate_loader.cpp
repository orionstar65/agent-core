#include "agent/certificate_loader.hpp"
#include "agent/config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/stack.h>
#endif

namespace fs = std::filesystem;

namespace agent {

namespace {

std::string trim_whitespace(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string read_file_content(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return trim_whitespace(content);
}

bool file_exists_and_readable(const std::string& file_path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(file_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return _access(file_path.c_str(), 4) == 0;
#else
    return access(file_path.c_str(), R_OK) == 0;
#endif
}

std::string get_installer_provisioned_path() {
#ifdef _WIN32
    const char* programdata = std::getenv("PROGRAMDATA");
    if (programdata) {
        return std::string(programdata) + "\\AgentCore\\certificates\\";
    }
    return "C:\\ProgramData\\AgentCore\\certificates\\";
#else
    if (access("/etc/agent-core/certificates/", R_OK) == 0) {
        return "/etc/agent-core/certificates/";
    }
    return "/var/lib/agent-core/certificates/";
#endif
}

#ifdef _WIN32
std::string load_certificate_from_windows_store(const Config::Cert& cert_config) {
    HCERTSTORE hStore = NULL;
    PCCERT_CONTEXT pCertContext = NULL;
    std::string result;
    
    DWORD dwStoreFlags = CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG;
    
    if (cert_config.store_location == "CURRENT_USER") {
        dwStoreFlags = CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG;
    }
    
    hStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM,
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0,
        dwStoreFlags,
        "MY"
    );
    
    if (hStore == NULL && cert_config.store_location != "CURRENT_USER") {
        dwStoreFlags = CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG;
        hStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0,
            dwStoreFlags,
            "MY"
        );
    }
    
    if (hStore == NULL) {
        return "";
    }
    
    DWORD dwFindType = 0;
    const void* pvFindPara = NULL;
    std::vector<BYTE> thumbprint_bytes;
    
    if (!cert_config.subject_name.empty()) {
        dwFindType = CERT_FIND_SUBJECT_STR_A;
        pvFindPara = cert_config.subject_name.c_str();
        pCertContext = CertFindCertificateInStore(
            hStore,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0,
            dwFindType,
            pvFindPara,
            NULL
        );
    } else if (!cert_config.thumbprint.empty()) {
        for (size_t i = 0; i < cert_config.thumbprint.length(); i += 2) {
            if (i + 1 < cert_config.thumbprint.length()) {
                std::string byte_str = cert_config.thumbprint.substr(i, 2);
                try {
                    thumbprint_bytes.push_back(static_cast<BYTE>(std::stoul(byte_str, nullptr, 16)));
                } catch (...) {
                    CertCloseStore(hStore, 0);
                    return "";
                }
            }
        }
        if (!thumbprint_bytes.empty() && thumbprint_bytes.size() == 20) {
            CRYPT_HASH_BLOB hashBlob;
            hashBlob.cbData = static_cast<DWORD>(thumbprint_bytes.size());
            hashBlob.pbData = thumbprint_bytes.data();
            pCertContext = CertFindCertificateInStore(
                hStore,
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                0,
                CERT_FIND_SHA1_HASH,
                &hashBlob,
                NULL
            );
        }
    } else {
        pCertContext = CertEnumCertificatesInStore(hStore, NULL);
    }
    
    if (pCertContext != NULL) {
        DWORD dwSize = 0;
        if (CryptBinaryToStringA(
            pCertContext->pbCertEncoded,
            pCertContext->cbCertEncoded,
            CRYPT_STRING_BASE64HEADER,
            NULL,
            &dwSize
        )) {
            std::vector<char> buffer(dwSize);
            if (CryptBinaryToStringA(
                pCertContext->pbCertEncoded,
                pCertContext->cbCertEncoded,
                CRYPT_STRING_BASE64HEADER,
                buffer.data(),
                &dwSize
            )) {
                result = std::string(buffer.data());
            }
        }
        CertFreeCertificateContext(pCertContext);
    }
    
    CertCloseStore(hStore, 0);
    return result;
}
#else
std::string load_certificate_from_linux_store(const Config::Cert& cert_config) {
    std::vector<std::string> search_paths = {
        "/etc/ssl/certs/",
        "/usr/share/ca-certificates/",
        "/etc/pki/tls/certs/"
    };
    
    if (!cert_config.subject_name.empty()) {
        X509_STORE* store = X509_STORE_new();
        if (!store) {
            return "";
        }
        
        for (const auto& path : search_paths) {
            X509_STORE_load_locations(store, NULL, path.c_str());
        }
        
        X509_STORE_CTX* ctx = X509_STORE_CTX_new();
        if (!ctx) {
            X509_STORE_free(store);
            return "";
        }
        
        X509_STORE_CTX_init(ctx, store, NULL, NULL);
        
        X509* cert = NULL;
        X509_STORE_CTX_set_purpose(ctx, X509_PURPOSE_SSL_CLIENT);
        
        STACK_OF(X509)* certs = X509_STORE_get1_all_certs(store);
        if (certs) {
            for (int i = 0; i < sk_X509_num(certs); i++) {
                X509* x = sk_X509_value(certs, i);
                X509_NAME* name = X509_get_subject_name(x);
                char buf[256];
                X509_NAME_oneline(name, buf, sizeof(buf));
                
                if (cert_config.subject_name.find(buf) != std::string::npos ||
                    strstr(buf, cert_config.subject_name.c_str()) != NULL) {
                    cert = X509_dup(x);
                    break;
                }
            }
            sk_X509_pop_free(certs, X509_free);
        }
        
        X509_STORE_CTX_free(ctx);
        X509_STORE_free(store);
        
        if (cert) {
            BIO* bio = BIO_new(BIO_s_mem());
            if (PEM_write_bio_X509(bio, cert)) {
                BUF_MEM* buf_mem;
                BIO_get_mem_ptr(bio, &buf_mem);
                std::string result(buf_mem->data, buf_mem->length);
                BIO_free(bio);
                X509_free(cert);
                return trim_whitespace(result);
            }
            BIO_free(bio);
            X509_free(cert);
        }
    }
    
    if (!cert_config.thumbprint.empty()) {
        for (const auto& path : search_paths) {
            try {
                if (fs::exists(path) && fs::is_directory(path)) {
                    for (const auto& entry : fs::directory_iterator(path)) {
                        if (entry.is_regular_file() && 
                            (entry.path().extension() == ".crt" || 
                             entry.path().extension() == ".pem")) {
                            std::string cert_file = entry.path().string();
                            std::string content = read_file_content(cert_file);
                            if (!content.empty()) {
                                return content;
                            }
                        }
                    }
                }
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    
    return "";
}
#endif

} 

CertificateLoadResult load_certificate(const Config& config) {
    CertificateLoadResult result;
    
    const char* env_cert_path = std::getenv("AGENT_CERT_PATH");
    if (env_cert_path && strlen(env_cert_path) > 0) {
        std::string env_path(env_cert_path);
        if (file_exists_and_readable(env_path)) {
            result.certificate_content = read_file_content(env_path);
            if (!result.certificate_content.empty()) {
                result.success = true;
                result.source_description = "Environment variable AGENT_CERT_PATH";
                return result;
            }
        }
        result.error_message = "Certificate file from AGENT_CERT_PATH not found or unreadable: " + env_path;
        return result;
    }
    
    if (config.cert.store_hint == "OS" || config.cert.store_hint.empty()) {
#ifdef _WIN32
        std::string cert_content = load_certificate_from_windows_store(config.cert);
        if (!cert_content.empty()) {
            result.certificate_content = cert_content;
            result.success = true;
            result.source_description = "Windows Certificate Store";
            return result;
        }
#else
        std::string cert_content = load_certificate_from_linux_store(config.cert);
        if (!cert_content.empty()) {
            result.certificate_content = cert_content;
            result.success = true;
            result.source_description = "Linux Certificate Store";
            return result;
        }
#endif
        result.error_message = "Certificate not found in OS certificate store";
    }
    
    std::string installer_path = get_installer_provisioned_path();
    if (fs::exists(installer_path) && fs::is_directory(installer_path)) {
        try {
            for (const auto& entry : fs::directory_iterator(installer_path)) {
                if (entry.is_regular_file() && 
                    (entry.path().extension() == ".crt" || 
                     entry.path().extension() == ".pem" ||
                     entry.path().extension() == ".txt")) {
                    std::string cert_file = entry.path().string();
                    std::string content = read_file_content(cert_file);
                    if (!content.empty()) {
                        result.certificate_content = content;
                        result.success = true;
                        result.source_description = "Installer-provisioned location: " + installer_path;
                        return result;
                    }
                }
            }
        } catch (const std::exception& e) {
            result.error_message = "Error reading installer-provisioned certificates: " + std::string(e.what());
        }
    }
    
    if (!config.cert.cert_path.empty()) {
        std::string cert_path = config.cert.cert_path;
        if (file_exists_and_readable(cert_path)) {
            result.certificate_content = read_file_content(cert_path);
            if (!result.certificate_content.empty()) {
                result.success = true;
                result.source_description = "Explicit config path: " + cert_path;
                return result;
            }
        }
        result.error_message = "Certificate file not found or unreadable: " + cert_path;
        return result;
    }
    
    if (!result.success && result.error_message.empty()) {
        result.error_message = "No certificate found. Checked: "
                              "1) AGENT_CERT_PATH environment variable, "
                              "2) OS certificate store, "
                              "3) Installer-provisioned location, "
                              "4) Explicit config path";
    }
    
    return result;
}

std::string resolve_certificate_path(const Config& config) {
    const char* env_cert_path = std::getenv("AGENT_CERT_PATH");
    if (env_cert_path && strlen(env_cert_path) > 0) {
        return std::string(env_cert_path);
    }
    
    if (!config.cert.cert_path.empty()) {
        return config.cert.cert_path;
    }
    
    return get_installer_provisioned_path();
}

bool validate_certificate_configuration(const Config& config, std::string& error_message) {
    const char* env_cert_path = std::getenv("AGENT_CERT_PATH");
    if (env_cert_path && strlen(env_cert_path) > 0) {
        if (file_exists_and_readable(std::string(env_cert_path))) {
            return true;
        }
        error_message = "AGENT_CERT_PATH points to non-existent or unreadable file: " + std::string(env_cert_path);
        return false;
    }
    
    if (config.cert.store_hint == "OS" || config.cert.store_hint.empty()) {
        return true;
    }
    
    if (!config.cert.cert_path.empty()) {
        if (file_exists_and_readable(config.cert.cert_path)) {
            return true;
        }
        error_message = "Certificate path in config points to non-existent or unreadable file: " + config.cert.cert_path;
        return false;
    }
    
    std::string installer_path = get_installer_provisioned_path();
    if (fs::exists(installer_path) && fs::is_directory(installer_path)) {
        try {
            bool found = false;
            for (const auto& entry : fs::directory_iterator(installer_path)) {
                if (entry.is_regular_file()) {
                    found = true;
                    break;
                }
            }
            if (found) {
                return true;
            }
        } catch (const std::exception&) {
        }
    }
    
    error_message = "No certificate configuration found. Please set AGENT_CERT_PATH environment variable, "
                   "configure OS certificate store, provide installer-provisioned certificate, or specify cert_path in config.";
    return false;
}

}
