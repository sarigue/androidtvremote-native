#pragma once

#include <cstdint>
#include <string>

namespace androidtvremote {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
};

enum class Direction : std::int32_t {
    Unknown = 0,
    StartLong = 1,
    EndLong = 2,
    Short = 3,
};

// Android key codes used by Freebox Pop Remote and common TV controls.
// sendKey(int32_t, ...) also accepts any Android key code not listed here.
enum class KeyCode : std::int32_t {
    Unknown = 0,
    Home = 3,
    Back = 4,
    Digit0 = 7,
    Digit1 = 8,
    Digit2 = 9,
    Digit3 = 10,
    Digit4 = 11,
    Digit5 = 12,
    Digit6 = 13,
    Digit7 = 14,
    Digit8 = 15,
    Digit9 = 16,
    DpadUp = 19,
    DpadDown = 20,
    DpadLeft = 21,
    DpadRight = 22,
    DpadCenter = 23,
    VolumeUp = 24,
    VolumeDown = 25,
    Power = 26,
    Enter = 66,
    Delete = 67,
    Search = 84,
    MediaPlayPause = 85,
    PageUp = 92,
    PageDown = 93,
    VolumeMute = 164,
    ChannelUp = 166,
    ChannelDown = 167,
};

struct DeviceInfo {
    std::string manufacturer;
    std::string model;
    std::string softwareVersion;
};

struct VolumeInfo {
    std::uint32_t level = 0;
    std::uint32_t maximum = 0;
    bool muted = false;
};

struct DeviceIdentity {
    std::string name;
    std::string macAddress;
};

}  // namespace androidtvremote
