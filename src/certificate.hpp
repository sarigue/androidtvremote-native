#pragma once

#include "androidtvremote/types.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace androidtvremote::crypto {

bool generateCertificateIfMissing(
    const std::filesystem::path& certificateFile,
    const std::filesystem::path& privateKeyFile,
    std::string_view commonName);

std::array<std::uint8_t, 32> pairingSecret(
    X509* clientCertificate,
    X509* serverCertificate,
    std::string_view sixHexDigitCode);

DeviceIdentity parseDeviceIdentity(X509* certificate);

void loadClientIdentity(
    SSL_CTX* context,
    const std::filesystem::path& certificateFile,
    const std::filesystem::path& privateKeyFile);

}  // namespace androidtvremote::crypto
