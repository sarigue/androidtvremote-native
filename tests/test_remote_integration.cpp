#include <androidtvremote/android_tv_remote.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;
using androidtvremote::AndroidTvRemote;
using androidtvremote::ConnectionState;
using androidtvremote::KeyCode;

namespace testwire {

void putVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

void putVarintField(std::vector<std::uint8_t>& out, std::uint32_t field, std::uint64_t value) {
    putVarint(out, static_cast<std::uint64_t>(field) << 3U);
    putVarint(out, value);
}

void putBytesField(std::vector<std::uint8_t>& out, std::uint32_t field, std::span<const std::uint8_t> bytes) {
    putVarint(out, (static_cast<std::uint64_t>(field) << 3U) | 2U);
    putVarint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void putStringField(std::vector<std::uint8_t>& out, std::uint32_t field, const std::string& value) {
    putBytesField(out, field, {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}

std::vector<std::uint8_t> nested(std::uint32_t outerField, const std::vector<std::uint8_t>& inner) {
    std::vector<std::uint8_t> out;
    putBytesField(out, outerField, inner);
    return out;
}

std::vector<std::uint8_t> configure() {
    std::vector<std::uint8_t> info;
    putStringField(info, 1, "Fake TV");
    putStringField(info, 2, "Test Vendor");
    putStringField(info, 6, "1.2.3");

    std::vector<std::uint8_t> cfg;
    constexpr std::uint32_t features = (1U << 0) | (1U << 1) | (1U << 2) |
                                       (1U << 3) | (1U << 5) | (1U << 6) | (1U << 9);
    putVarintField(cfg, 1, features);
    putBytesField(cfg, 2, info);
    return nested(1, cfg);
}

std::vector<std::uint8_t> setActive() {
    std::vector<std::uint8_t> inner;
    putVarintField(inner, 1, 0);
    return nested(2, inner);
}

std::vector<std::uint8_t> start() {
    std::vector<std::uint8_t> inner;
    putVarintField(inner, 1, 1);
    return nested(40, inner);
}

std::vector<std::uint8_t> ping(std::int32_t value) {
    std::vector<std::uint8_t> inner;
    putVarintField(inner, 1, static_cast<std::uint32_t>(value));
    return nested(8, inner);
}

void sendFrame(asio::ssl::stream<tcp::socket>& stream, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> frame;
    putVarint(frame, payload.size());
    frame.insert(frame.end(), payload.begin(), payload.end());
    asio::write(stream, asio::buffer(frame));
}

std::vector<std::uint8_t> receiveFrame(asio::ssl::stream<tcp::socket>& stream) {
    std::uint64_t length = 0;
    unsigned shift = 0;
    while (true) {
        std::uint8_t byte = 0;
        asio::read(stream, asio::buffer(&byte, 1));
        length |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) break;
        shift += 7;
        assert(shift <= 63);
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
    if (!payload.empty()) asio::read(stream, asio::buffer(payload));
    return payload;
}

std::uint64_t readVarint(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    while (offset < bytes.size()) {
        const auto byte = bytes[offset++];
        result |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) return result;
        shift += 7;
    }
    assert(false && "truncated varint");
    return 0;
}

std::span<const std::uint8_t> findBytes(std::span<const std::uint8_t> bytes, std::uint32_t wanted) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto key = readVarint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(key >> 3U);
        const auto type = static_cast<unsigned>(key & 7U);
        if (type == 0) {
            (void)readVarint(bytes, offset);
        } else if (type == 2) {
            const auto size = static_cast<std::size_t>(readVarint(bytes, offset));
            assert(size <= bytes.size() - offset);
            auto value = bytes.subspan(offset, size);
            if (field == wanted) return value;
            offset += size;
        } else {
            assert(false && "unexpected wire type in integration test");
        }
    }
    return {};
}

std::uint64_t findVarint(std::span<const std::uint8_t> bytes, std::uint32_t wanted) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto key = readVarint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(key >> 3U);
        const auto type = static_cast<unsigned>(key & 7U);
        if (type == 0) {
            const auto value = readVarint(bytes, offset);
            if (field == wanted) return value;
        } else if (type == 2) {
            const auto size = static_cast<std::size_t>(readVarint(bytes, offset));
            offset += size;
        } else {
            assert(false && "unexpected wire type in integration test");
        }
    }
    assert(false && "field not found");
    return 0;
}

}  // namespace testwire

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
                     ("androidtvremote-integration-" + std::to_string(nonce));
    std::filesystem::create_directories(dir);
    const auto cert = dir / "cert.pem";
    const auto key = dir / "key.pem";

    AndroidTvRemote certificateMaker({
        .clientName = "integration-test",
        .certificateFile = cert,
        .privateKeyFile = key,
        .host = "127.0.0.1",
        .autoReconnect = false,
    });
    assert(certificateMaker.generateCertificateIfMissing());

    asio::io_context serverIo;
    asio::ssl::context serverContext(asio::ssl::context::tls_server);
    serverContext.set_verify_mode(asio::ssl::verify_none);
    serverContext.use_certificate_chain_file(cert.string());
    serverContext.use_private_key_file(key.string(), asio::ssl::context::pem);
    tcp::acceptor acceptor(serverIo, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor.local_endpoint().port();

    std::promise<void> serverReady;
    std::promise<void> serverSawHome;
    auto sawHome = serverSawHome.get_future();

    std::thread server([&] {
        serverReady.set_value();
        asio::ssl::stream<tcp::socket> stream(serverIo, serverContext);
        acceptor.accept(stream.next_layer());
        stream.handshake(asio::ssl::stream_base::server);

        testwire::sendFrame(stream, testwire::configure());
        const auto configureResponse = testwire::receiveFrame(stream);
        assert(!testwire::findBytes(configureResponse, 1).empty());

        testwire::sendFrame(stream, testwire::setActive());
        const auto activeResponse = testwire::receiveFrame(stream);
        assert(!testwire::findBytes(activeResponse, 2).empty());

        testwire::sendFrame(stream, testwire::start());
        const auto keyMessage = testwire::receiveFrame(stream);
        const auto keyInject = testwire::findBytes(keyMessage, 10);
        assert(!keyInject.empty());
        assert(testwire::findVarint(keyInject, 1) == static_cast<std::uint32_t>(KeyCode::Home));
        serverSawHome.set_value();

        testwire::sendFrame(stream, testwire::ping(123));
        const auto pingResponse = testwire::receiveFrame(stream);
        const auto ping = testwire::findBytes(pingResponse, 9);
        assert(!ping.empty());
        assert(testwire::findVarint(ping, 1) == 123);

        boost::system::error_code ignored;
        stream.shutdown(ignored);
        stream.next_layer().close(ignored);
    });

    serverReady.get_future().wait();

    std::promise<void> connectedPromise;
    auto connected = connectedPromise.get_future();
    bool connectedSignaled = false;
    AndroidTvRemote remote({
        .clientName = "integration-test",
        .certificateFile = cert,
        .privateKeyFile = key,
        .host = "127.0.0.1",
        .apiPort = port,
        .enableVoice = true,
        .autoReconnect = false,
    });
    remote.setCallbacks({
        .onConnectionState = [&](ConnectionState state) {
            if (state == ConnectionState::Connected && !connectedSignaled) {
                connectedSignaled = true;
                connectedPromise.set_value();
            }
        },
        .onError = [](const std::string& error) {
            std::cerr << "client error: " << error << '\n';
        },
    });
    remote.connect();
    assert(connected.wait_for(5s) == std::future_status::ready);
    remote.sendKey(KeyCode::Home);
    assert(sawHome.wait_for(5s) == std::future_status::ready);

    server.join();
    remote.disconnect();
    std::filesystem::remove_all(dir);
    std::cout << "androidtvremote TLS remote integration test: OK\n";
    return 0;
}
