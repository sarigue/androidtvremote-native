# androidtvremote-native

**English** · [Français](README.fr.md)

A native **C++20** client library for the **Android TV Remote v2** protocol, including the pairing protocol used by Android TV and Google TV devices.

The project is independent, has no Python or Protocol Buffers runtime dependency, and can be integrated into Qt, GTK, Win32, Cocoa, command-line, or service applications.

> Current version: **0.2.0**. The protocol implementation is covered by local simulated TLS tests, but compatibility should still be validated against each physical device and Android TV version.

## Features

- RSA-2048/X.509 client certificate generation with OpenSSL;
- Android TV pairing on port `6467`;
- TLS Remote v2 connection on port `6466`;
- built-in Protocol Buffers wire framing with no `libprotobuf` dependency;
- Android key events, D-pad, volume, channel, and number controls;
- App Links and text/IME input;
- power, foreground application, and volume state callbacks;
- automatic ping replies and reconnect backoff;
- 16-bit mono 8 kHz PCM voice input;
- asynchronous connection/control API and synchronous pairing API;
- static and shared library builds.

## Requirements

- a C++20 compiler;
- CMake 3.21 or newer;
- OpenSSL 1.1.1 or newer;
- Boost 1.74 or newer (Asio headers only);
- native system threads.

## Quick start

On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build libssl-dev libboost-dev
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DANDROIDTVREMOTE_BUILD_EXAMPLES=ON \
  -DANDROIDTVREMOTE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The static library is produced as `build/libandroidtvremote.a`. Set `BUILD_SHARED_LIBS=ON` for a shared library.

### Windows

The repository includes a `vcpkg.json` manifest. From a Visual Studio developer shell with vcpkg available:

```powershell
cmake -S . -B build -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

### macOS

```bash
brew install cmake ninja boost openssl@3
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The provided `Makefile` also exposes `make native-test` and cross-compilation targets. See [CROSS_BUILD.md](CROSS_BUILD.md) for the full Linux, Windows, and macOS target matrix.

## CMake integration

Install the project to a prefix:

```bash
cmake --install build --prefix /path/to/prefix
```

Then consume it from another CMake project:

```cmake
find_package(androidtvremote CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE androidtvremote::androidtvremote)
```

## Usage

```cpp
#include <androidtvremote/android_tv_remote.hpp>

using namespace androidtvremote;

AndroidTvRemote remote({
    .clientName = "Living Room Remote",
    .certificateFile = "cert.pem",
    .privateKeyFile = "key.pem",
    .host = "192.168.1.42",
    .enableVoice = true,
});

remote.setCallbacks({
    .onConnectionState = [](ConnectionState state) {
        // Forward to the GUI thread when used from a graphical application.
    },
    .onAuthenticationRequired = [] {
        // Start the pairing user flow.
    },
    .onError = [](const std::string& error) {
        // Log or display the error.
    },
});

if (remote.generateCertificateIfMissing()) {
    remote.startPairing();
    // Ask the user for the six-character code shown on the TV.
    remote.finishPairing("A1B2C3");
}

remote.connect();
remote.sendKey(KeyCode::Home);
remote.launchApp("https://example.com/");
```

Callbacks run on the library's internal I/O thread. A graphical application must dispatch them to its main thread. Pairing operations are intentionally synchronous and should run in a worker thread in GUI applications.

Generated certificates and private keys are credentials: keep them outside source control and protect them as application secrets.

## Voice input

`startVoice()` sends `KEYCODE_SEARCH`, waits for `remote_voice_begin`, and reports the `session_id` through `onVoiceStarted`. `sendVoiceData()` accepts 16-bit mono 8 kHz PCM. Chunks are limited to 20 KiB and padded to at least 8 KiB to match Android TV expectations.

## Continuous integration

GitHub Actions builds and tests both static and shared variants on Linux, macOS, and Windows. Every successful job publishes its archived CMake install tree as a short-lived workflow artifact. The workflow runs on pull requests, pushes, and manual dispatch.

## Project documentation

- [French README](README.fr.md)
- [Cross-compilation guide (French)](CROSS_BUILD.md)
- [Migration from the Python implementation (French)](MIGRATION_FROM_PYTHON.md)
- [Contributing guide](CONTRIBUTING.md)
- [Code of conduct](CODE_OF_CONDUCT.md)
- [Security policy](SECURITY.md)
- [Changelog](CHANGELOG.md)

## Protocol references

This clean, minimal wire-codec implementation is informed by publicly available Android TV Remote v2 behavior and protocol material, including `tronikos/androidtvremote2` (Apache-2.0) and the Google TV pairing protocol materials published in AOSP.

Android and Google TV are trademarks of their respective owners. This project is not affiliated with or endorsed by Google.

## License

Licensed under the [Apache License 2.0](LICENSE). Attribution information is available in [NOTICE](NOTICE).
