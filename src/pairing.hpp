#pragma once

#include "androidtvremote/types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace androidtvremote {

class PairingSession {
public:
    PairingSession(
        std::string clientName,
        std::filesystem::path certificateFile,
        std::filesystem::path privateKeyFile,
        std::string host,
        std::uint16_t port);
    ~PairingSession();

    PairingSession(const PairingSession&) = delete;
    PairingSession& operator=(const PairingSession&) = delete;

    void start();
    void finish(std::string_view code);
    DeviceIdentity getNameAndMac();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace androidtvremote
