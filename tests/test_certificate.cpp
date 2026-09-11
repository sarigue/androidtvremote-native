#include <androidtvremote/android_tv_remote.hpp>
#include <androidtvremote/key_codes.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using namespace androidtvremote;

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
                     ("androidtvremote-test-" + std::to_string(nonce));
    std::filesystem::create_directories(dir);

    {
        AndroidTvRemote remote({
            .clientName = "libandroidtvremote-test",
            .certificateFile = dir / "cert.pem",
            .privateKeyFile = dir / "key.pem",
            .host = "127.0.0.1",
        });
        assert(remote.generateCertificateIfMissing());
        assert(!remote.generateCertificateIfMissing());
        assert(std::filesystem::file_size(dir / "cert.pem") > 0);
        assert(std::filesystem::file_size(dir / "key.pem") > 0);
    }

    assert(keyCodeFromName("POWER").value() == 26);
    assert(keyCodeFromName("keycode_dpad_up").value() == 19);
    assert(keyCodeFromName("0").value() == 7);
    assert(keyCodeFromName("MUTE").value() == 91);
    assert(keyCodeFromName("VOLUME_MUTE").value() == 164);
    assert(!keyCodeFromName("NOT_A_KEY").has_value());

    std::filesystem::remove_all(dir);
    std::cout << "androidtvremote certificate/API tests: OK\n";
    return 0;
}
