#pragma once

#include "androidtvremote/export.hpp"
#include "androidtvremote/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace androidtvremote {

class ANDROIDTVREMOTE_API AndroidTvRemote {
public:
    struct Options {
        std::string clientName = "Android TV Remote";
        std::filesystem::path certificateFile;
        std::filesystem::path privateKeyFile;
        std::string host;
        std::uint16_t apiPort = 6466;
        std::uint16_t pairingPort = 6467;
        bool enableIme = true;
        bool enableVoice = false;
        bool autoReconnect = true;
    };

    struct Callbacks {
        // Callbacks are invoked on the library's internal I/O thread.
        std::function<void(ConnectionState)> onConnectionState;
        std::function<void(bool)> onPowerChanged;
        std::function<void(const std::string&)> onCurrentAppChanged;
        std::function<void(const VolumeInfo&)> onVolumeChanged;
        std::function<void(const DeviceInfo&)> onDeviceInfoChanged;
        std::function<void(bool)> onAvailabilityChanged;
        // Equivalent to androidtvremote2's InvalidAuth path: the stored
        // certificate is missing from the TV or pairing must be performed again.
        std::function<void()> onAuthenticationRequired;
        std::function<void(std::int32_t)> onVoiceStarted;
        std::function<void()> onVoiceStopped;
        std::function<void(const std::string&)> onError;
    };

    explicit AndroidTvRemote(Options options);
    ~AndroidTvRemote();

    AndroidTvRemote(const AndroidTvRemote&) = delete;
    AndroidTvRemote& operator=(const AndroidTvRemote&) = delete;
    AndroidTvRemote(AndroidTvRemote&&) noexcept;
    AndroidTvRemote& operator=(AndroidTvRemote&&) noexcept;

    void setCallbacks(Callbacks callbacks);

    // Certificate / pairing operations are synchronous. Run them from a worker
    // thread when integrating with a GUI.
    bool generateCertificateIfMissing();
    void startPairing();
    void finishPairing(std::string_view sixHexDigitCode);
    DeviceIdentity getNameAndMac();

    // Remote connection and commands are asynchronous and return immediately.
    void connect();
    void disconnect();
    [[nodiscard]] ConnectionState connectionState() const noexcept;

    void sendKey(KeyCode key, Direction direction = Direction::Short);
    void sendKey(std::int32_t androidKeyCode, Direction direction = Direction::Short);
    void sendKey(std::string_view keyName, Direction direction = Direction::Short);
    void sendText(std::string text);
    void launchApp(std::string appLink);

    void startVoice();
    void sendVoiceData(std::span<const std::byte> pcm16Mono8kHz);
    void sendVoiceData(std::span<const std::uint8_t> pcm16Mono8kHz);
    void stopVoice();

    [[nodiscard]] bool isVoiceEnabled() const noexcept;
    [[nodiscard]] bool isOn() const noexcept;
    [[nodiscard]] std::string currentApp() const;
    [[nodiscard]] DeviceInfo deviceInfo() const;
    [[nodiscard]] VolumeInfo volumeInfo() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace androidtvremote
