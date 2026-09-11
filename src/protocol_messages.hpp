#pragma once

#include "androidtvremote/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace androidtvremote::protocol {

constexpr std::int32_t FeaturePing = 1 << 0;
constexpr std::int32_t FeatureKey = 1 << 1;
constexpr std::int32_t FeatureIme = 1 << 2;
constexpr std::int32_t FeatureVoice = 1 << 3;
constexpr std::int32_t FeaturePower = 1 << 5;
constexpr std::int32_t FeatureVolume = 1 << 6;
constexpr std::int32_t FeatureAppLink = 1 << 9;

std::vector<std::uint8_t> pairingRequest(std::string_view clientName);
std::vector<std::uint8_t> pairingOptions();
std::vector<std::uint8_t> pairingConfiguration();
std::vector<std::uint8_t> pairingSecret(std::span<const std::uint8_t> secret);

[[nodiscard]] std::optional<std::uint64_t> pairingStatus(std::span<const std::uint8_t> message);
[[nodiscard]] bool hasPairingRequestAck(std::span<const std::uint8_t> message);
[[nodiscard]] bool hasPairingOptions(std::span<const std::uint8_t> message);
[[nodiscard]] bool hasConfigurationAck(std::span<const std::uint8_t> message);
[[nodiscard]] bool hasSecretAck(std::span<const std::uint8_t> message);

std::vector<std::uint8_t> remoteConfigureResponse(std::int32_t activeFeatures);
std::vector<std::uint8_t> remoteSetActiveResponse(std::int32_t activeFeatures);
std::vector<std::uint8_t> remotePingResponse(std::int32_t value);
std::vector<std::uint8_t> remoteKey(std::int32_t keyCode, Direction direction);
std::vector<std::uint8_t> remoteText(std::string_view text, std::int32_t imeCounter, std::int32_t fieldCounter);
std::vector<std::uint8_t> remoteAppLink(std::string_view appLink);
std::vector<std::uint8_t> remoteVoiceBegin(std::int32_t sessionId);
std::vector<std::uint8_t> remoteVoicePayload(std::int32_t sessionId, std::span<const std::uint8_t> samples);
std::vector<std::uint8_t> remoteVoiceEnd(std::int32_t sessionId);

struct RemoteIncoming {
    enum class Kind {
        Unknown,
        Configure,
        SetActive,
        ImeKeyInject,
        ImeBatchEdit,
        Volume,
        Start,
        Ping,
        Error,
        VoiceBegin,
    } kind = Kind::Unknown;

    std::int32_t supportedFeatures = 0;
    DeviceInfo deviceInfo;
    std::string currentApp;
    std::int32_t imeCounter = 0;
    std::int32_t fieldCounter = 0;
    VolumeInfo volume;
    bool started = false;
    std::int32_t pingValue = 0;
    std::int32_t voiceSessionId = 0;
};

[[nodiscard]] RemoteIncoming parseRemote(std::span<const std::uint8_t> message);

}  // namespace androidtvremote::protocol
