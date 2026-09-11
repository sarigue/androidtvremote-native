#include "certificate.hpp"

#include <openssl/bn.h>
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#endif
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace androidtvremote::crypto {
namespace {

template <typename T, void (*FreeFn)(T*)>
using OsslPtr = std::unique_ptr<T, decltype(FreeFn)>;

std::string lastOpenSslError(std::string_view prefix) {
    const auto code = ERR_get_error();
    if (code == 0) {
        return std::string(prefix);
    }
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return std::string(prefix) + ": " + buffer;
}

std::vector<std::uint8_t> hexToBytes(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("hex string must have an even number of characters");
    }
    auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + c - 'A');
        throw std::invalid_argument("pairing code is not hexadecimal");
    };

    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4U) | nibble(hex[i + 1])));
    }
    return out;
}

std::vector<std::uint8_t> bnBytes(const BIGNUM* value) {
    const int size = BN_num_bytes(value);
    if (size <= 0) {
        throw std::runtime_error("RSA public number is empty");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (BN_bn2bin(value, bytes.data()) != size) {
        throw std::runtime_error(lastOpenSslError("BN_bn2bin failed"));
    }
    return bytes;
}

std::pair<const BIGNUM*, const BIGNUM*> rsaNumbers(X509* certificate) {
    OsslPtr<EVP_PKEY, EVP_PKEY_free> key(X509_get_pubkey(certificate), EVP_PKEY_free);
    if (!key) {
        throw std::runtime_error(lastOpenSslError("certificate has no public key"));
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    BIGNUM* n = nullptr;
    BIGNUM* e = nullptr;
    if (EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_RSA_N, &n) != 1 ||
        EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_RSA_E, &e) != 1) {
        BN_free(n);
        BN_free(e);
        throw std::runtime_error(lastOpenSslError("unable to extract RSA public numbers"));
    }
    // The caller needs ownership in this branch; store in thread-local holders so
    // the returned pointers remain valid until the next call on this thread.
    thread_local OsslPtr<BIGNUM, BN_free> nHolder(nullptr, BN_free);
    thread_local OsslPtr<BIGNUM, BN_free> eHolder(nullptr, BN_free);
    nHolder.reset(n);
    eHolder.reset(e);
    return {nHolder.get(), eHolder.get()};
#else
    RSA* rsa = EVP_PKEY_get1_RSA(key.get());
    if (!rsa) {
        throw std::runtime_error(lastOpenSslError("public key is not RSA"));
    }
    thread_local OsslPtr<RSA, RSA_free> rsaHolder(nullptr, RSA_free);
    rsaHolder.reset(rsa);
    const BIGNUM* n = nullptr;
    const BIGNUM* e = nullptr;
    RSA_get0_key(rsaHolder.get(), &n, &e, nullptr);
    return {n, e};
#endif
}

void addExtension(X509* certificate, X509* issuer, int nid, const char* value) {
    X509V3_CTX context{};
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &context, nid, const_cast<char*>(value));
    if (!ext) {
        throw std::runtime_error(lastOpenSslError("failed to create X.509 extension"));
    }
    const int ok = X509_add_ext(certificate, ext, -1);
    X509_EXTENSION_free(ext);
    if (ok != 1) {
        throw std::runtime_error(lastOpenSslError("failed to add X.509 extension"));
    }
}

std::string nameEntry(X509_NAME* name, int nid) {
    const int index = X509_NAME_get_index_by_NID(name, nid, -1);
    if (index < 0) return {};
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, index);
    if (!entry) return {};
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    unsigned char* utf8 = nullptr;
    const int length = ASN1_STRING_to_UTF8(&utf8, data);
    if (length < 0 || !utf8) return {};
    std::string result(reinterpret_cast<char*>(utf8), static_cast<std::size_t>(length));
    OPENSSL_free(utf8);
    return result;
}


std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to open TLS identity file: " + path.string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void writeBioToFile(BIO* bio, const std::filesystem::path& path) {
    char* data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    if (size <= 0 || !data) {
        throw std::runtime_error(lastOpenSslError("failed to retrieve generated PEM data"));
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || !stream.write(data, static_cast<std::streamsize>(size))) {
        throw std::runtime_error("failed to write TLS identity file: " + path.string());
    }
}

std::vector<std::string> splitSlash(const std::string& value) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (true) {
        const auto pos = value.find('/', begin);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(begin));
            break;
        }
        parts.push_back(value.substr(begin, pos - begin));
        begin = pos + 1;
    }
    return parts;
}

}  // namespace

bool generateCertificateIfMissing(
    const std::filesystem::path& certificateFile,
    const std::filesystem::path& privateKeyFile,
    std::string_view commonName) {
    if (std::filesystem::is_regular_file(certificateFile) &&
        std::filesystem::is_regular_file(privateKeyFile)) {
        return false;
    }

    if (!certificateFile.parent_path().empty()) {
        std::filesystem::create_directories(certificateFile.parent_path());
    }
    if (!privateKeyFile.parent_path().empty()) {
        std::filesystem::create_directories(privateKeyFile.parent_path());
    }

    OsslPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free> keyContext(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    if (!keyContext || EVP_PKEY_keygen_init(keyContext.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(keyContext.get(), 2048) <= 0) {
        throw std::runtime_error(lastOpenSslError("failed to initialize RSA key generation"));
    }

    EVP_PKEY* rawKey = nullptr;
    if (EVP_PKEY_keygen(keyContext.get(), &rawKey) <= 0) {
        throw std::runtime_error(lastOpenSslError("failed to generate RSA key"));
    }
    OsslPtr<EVP_PKEY, EVP_PKEY_free> key(rawKey, EVP_PKEY_free);

    OsslPtr<X509, X509_free> certificate(X509_new(), X509_free);
    if (!certificate) {
        throw std::runtime_error(lastOpenSslError("failed to allocate X.509 certificate"));
    }

    X509_set_version(certificate.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1000);
    X509_gmtime_adj(X509_get_notBefore(certificate.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(certificate.get()), 60L * 60L * 24L * 365L * 10L);
    if (X509_set_pubkey(certificate.get(), key.get()) != 1) {
        throw std::runtime_error(lastOpenSslError("failed to set certificate public key"));
    }

    X509_NAME* name = X509_get_subject_name(certificate.get());
    const std::string cn(commonName);
    if (X509_NAME_add_entry_by_txt(
            name,
            "CN",
            MBSTRING_UTF8,
            reinterpret_cast<const unsigned char*>(cn.c_str()),
            -1,
            -1,
            0) != 1 ||
        X509_set_issuer_name(certificate.get(), name) != 1) {
        throw std::runtime_error(lastOpenSslError("failed to set certificate subject"));
    }

    addExtension(certificate.get(), certificate.get(), NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    const std::string san = "DNS:" + cn;
    addExtension(certificate.get(), certificate.get(), NID_subject_alt_name, san.c_str());

    if (X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0) {
        throw std::runtime_error(lastOpenSslError("failed to sign certificate"));
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> certBio(BIO_new(BIO_s_mem()), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new(BIO_s_mem()), BIO_free);
    if (!certBio || !keyBio) {
        throw std::runtime_error(lastOpenSslError("failed to allocate PEM output BIO"));
    }
    if (PEM_write_bio_X509(certBio.get(), certificate.get()) != 1 ||
        PEM_write_bio_PrivateKey(keyBio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        throw std::runtime_error(lastOpenSslError("failed to write certificate/key PEM files"));
    }
    writeBioToFile(certBio.get(), certificateFile);
    writeBioToFile(keyBio.get(), privateKeyFile);

    return true;
}

std::array<std::uint8_t, 32> pairingSecret(
    X509* clientCertificate,
    X509* serverCertificate,
    std::string_view sixHexDigitCode) {
    if (sixHexDigitCode.size() != 6) {
        throw std::invalid_argument("pairing code must contain exactly 6 hexadecimal digits");
    }
    const auto codeBytes = hexToBytes(sixHexDigitCode);

    const auto [clientN, clientE] = rsaNumbers(clientCertificate);
    // BN_bn2bin yields the same minimal big-endian representation as the
    // Python reference implementation's bytes.fromhex(f"{value:X}").  For
    // the usual public exponent 65537 this is 01 00 01, matching
    // bytes.fromhex(f"0{65537:X}").
    const auto clientNBytes = bnBytes(clientN);
    const auto clientEBytes = bnBytes(clientE);

    // rsaNumbers uses thread-local holders, so preserve the client's bytes
    // before extracting the server public key.
    const auto [serverN, serverE] = rsaNumbers(serverCertificate);
    const auto serverNBytes = bnBytes(serverN);
    const auto serverEBytes = bnBytes(serverE);

    EVP_MD_CTX* rawContext = EVP_MD_CTX_new();
    if (!rawContext) throw std::runtime_error(lastOpenSslError("failed to allocate SHA-256 context"));
    OsslPtr<EVP_MD_CTX, EVP_MD_CTX_free> context(rawContext, EVP_MD_CTX_free);
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error(lastOpenSslError("SHA-256 initialization failed"));
    }
    const auto update = [&](const auto& bytes) {
        if (!bytes.empty() && EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1) {
            throw std::runtime_error(lastOpenSslError("SHA-256 update failed"));
        }
    };
    update(clientNBytes);
    update(clientEBytes);
    update(serverNBytes);
    update(serverEBytes);
    const std::span<const std::uint8_t> codeTail(codeBytes.data() + 1, codeBytes.size() - 1);
    update(codeTail);

    std::array<std::uint8_t, 32> digest{};
    unsigned int digestLength = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digestLength) != 1 || digestLength != digest.size()) {
        throw std::runtime_error(lastOpenSslError("SHA-256 finalization failed"));
    }
    if (digest[0] != codeBytes[0]) {
        throw std::invalid_argument("pairing code does not match the TLS certificates");
    }
    return digest;
}

DeviceIdentity parseDeviceIdentity(X509* certificate) {
    if (!certificate) return {};
    X509_NAME* subject = X509_get_subject_name(certificate);
    const std::string commonName = nameEntry(subject, NID_commonName);
    const std::string qualifier = nameEntry(subject, NID_dnQualifier);
    const auto commonParts = splitSlash(commonName);
    const auto qualifierParts = splitSlash(qualifier);

    DeviceIdentity result;
    if (!qualifier.empty() && !qualifierParts.empty()) {
        result.name = qualifierParts.back();
    } else if (commonParts.size() > 1) {
        result.name = commonParts[commonParts.size() - 2];
    } else {
        result.name = commonName;
    }
    if (!commonParts.empty()) {
        result.macAddress = commonParts.back();
    }
    return result;
}


void loadClientIdentity(
    SSL_CTX* context,
    const std::filesystem::path& certificateFile,
    const std::filesystem::path& privateKeyFile) {
    if (!context) throw std::invalid_argument("SSL_CTX is null");
    const std::string certPem = readFile(certificateFile);
    const std::string keyPem = readFile(privateKeyFile);

    std::unique_ptr<BIO, decltype(&BIO_free)> certBio(
        BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size())), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(
        BIO_new_mem_buf(keyPem.data(), static_cast<int>(keyPem.size())), BIO_free);
    if (!certBio || !keyBio) {
        throw std::runtime_error(lastOpenSslError("unable to allocate PEM BIO"));
    }

    OsslPtr<X509, X509_free> certificate(
        PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr), X509_free);
    OsslPtr<EVP_PKEY, EVP_PKEY_free> privateKey(
        PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!certificate || !privateKey) {
        throw std::runtime_error(lastOpenSslError("unable to parse TLS certificate/private key"));
    }
    if (SSL_CTX_use_certificate(context, certificate.get()) != 1 ||
        SSL_CTX_use_PrivateKey(context, privateKey.get()) != 1 ||
        SSL_CTX_check_private_key(context) != 1) {
        throw std::runtime_error(lastOpenSslError("unable to install TLS certificate/private key"));
    }
}

}  // namespace androidtvremote::crypto
