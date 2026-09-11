#include <androidtvremote/android_tv_remote.hpp>
#include <androidtvremote/key_codes.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace androidtvremote;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage:\n"
                  << "  androidtvremote-cli <host> <data-dir> pair\n"
                  << "  androidtvremote-cli <host> <data-dir> key <KEY_NAME>\n";
        return 2;
    }

    const std::string host = argv[1];
    const std::filesystem::path dir = argv[2];
    const std::string command = argv[3];

    AndroidTvRemote remote({
        .clientName = "libandroidtvremote",
        .certificateFile = dir / "cert.pem",
        .privateKeyFile = dir / "key.pem",
        .host = host,
        .enableVoice = true,
    });

    try {
        const bool generated = remote.generateCertificateIfMissing();
        if (command == "pair") {
            remote.startPairing();
            std::cout << "Pairing code displayed by Android TV: ";
            std::string code;
            std::cin >> code;
            remote.finishPairing(code);
            std::cout << "Pairing complete\n";
            return 0;
        }

        if (generated) {
            std::cerr << "A new certificate was generated; pair the device first.\n";
            return 3;
        }

        remote.setCallbacks({
            .onConnectionState = [](ConnectionState state) {
                if (state == ConnectionState::Connected) std::cout << "Connected\n";
            },
            .onError = [](const std::string& error) { std::cerr << "Error: " << error << '\n'; },
        });
        remote.connect();
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (command == "key" && argc >= 5) {
            const auto key = keyCodeFromName(argv[4]);
            if (!key) {
                std::cerr << "Unknown key name: " << argv[4] << '\n';
                return 4;
            }
            remote.sendKey(*key);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return 0;
        }

        std::cerr << "Unknown command\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Fatal: " << error.what() << '\n';
        return 1;
    }
}
