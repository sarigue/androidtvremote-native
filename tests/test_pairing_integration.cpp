#include <androidtvremote/android_tv_remote.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using androidtvremote::AndroidTvRemote;

namespace pwire {
void putVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}
void varintField(std::vector<std::uint8_t>& out, std::uint32_t field, std::uint64_t value) {
    putVarint(out, static_cast<std::uint64_t>(field) << 3U);
    putVarint(out, value);
}
void bytesField(std::vector<std::uint8_t>& out, std::uint32_t field, std::span<const std::uint8_t> bytes) {
    putVarint(out, (static_cast<std::uint64_t>(field) << 3U) | 2U);
    putVarint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}
std::vector<std::uint8_t> outer(std::uint32_t field, const std::vector<std::uint8_t>& inner = {}) {
    std::vector<std::uint8_t> message;
    varintField(message, 1, 2);
    varintField(message, 2, 200);
    bytesField(message, field, inner);
    return message;
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
    std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
    if (!result.empty()) asio::read(stream, asio::buffer(result));
    return result;
}
std::uint64_t getVarint(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    while (offset < bytes.size()) {
        const auto byte = bytes[offset++];
        result |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) return result;
        shift += 7;
    }
    assert(false);
    return 0;
}
std::span<const std::uint8_t> findBytes(std::span<const std::uint8_t> bytes, std::uint32_t wanted) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto key = getVarint(bytes, offset);
        const auto field = static_cast<std::uint32_t>(key >> 3U);
        const auto type = static_cast<unsigned>(key & 7U);
        if (type == 0) {
            (void)getVarint(bytes, offset);
        } else if (type == 2) {
            const auto size = static_cast<std::size_t>(getVarint(bytes, offset));
            assert(size <= bytes.size() - offset);
            auto value = bytes.subspan(offset, size);
            if (field == wanted) return value;
            offset += size;
        } else {
            assert(false);
        }
    }
    return {};
}
}  // namespace pwire

std::unique_ptr<X509, decltype(&X509_free)> loadCert(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    const std::string pem{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(
        BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    assert(bio);
    X509* cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
    assert(cert);
    return {cert, X509_free};
}

std::vector<std::uint8_t> bnBytes(const BIGNUM* bn) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(BN_num_bytes(bn)));
    assert(BN_bn2bin(bn, bytes.data()) == static_cast<int>(bytes.size()));
    return bytes;
}

std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> publicNumbers(X509* cert) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(X509_get_pubkey(cert), EVP_PKEY_free);
    assert(key);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    BIGNUM* n = nullptr;
    BIGNUM* e = nullptr;
    assert(EVP_PKEY_get_bn_param(key.get(), "n", &n) == 1);
    assert(EVP_PKEY_get_bn_param(key.get(), "e", &e) == 1);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> nPtr(n, BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> ePtr(e, BN_free);
    return {bnBytes(n), bnBytes(e)};
#else
    RSA* rsa = EVP_PKEY_get1_RSA(key.get());
    assert(rsa);
    const BIGNUM* n = nullptr;
    const BIGNUM* e = nullptr;
    RSA_get0_key(rsa, &n, &e, nullptr);
    auto result = std::make_pair(bnBytes(n), bnBytes(e));
    RSA_free(rsa);
    return result;
#endif
}

std::array<std::uint8_t, 32> expectedSecret(X509* client, X509* server, std::uint8_t tail1, std::uint8_t tail2) {
    const auto [cn, ce] = publicNumbers(client);
    const auto [sn, se] = publicNumbers(server);
    std::array<std::uint8_t, 2> tail{tail1, tail2};
    std::array<std::uint8_t, 32> digest{};
    unsigned int size = 0;
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    assert(context);
    assert(EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1);
    for (const auto* bytes : {&cn, &ce, &sn, &se}) {
        assert(EVP_DigestUpdate(context.get(), bytes->data(), bytes->size()) == 1);
    }
    assert(EVP_DigestUpdate(context.get(), tail.data(), tail.size()) == 1);
    assert(EVP_DigestFinal_ex(context.get(), digest.data(), &size) == 1);
    assert(size == digest.size());
    return digest;
}

std::string hexCode(const std::array<std::uint8_t, 32>& digest, std::uint8_t tail1, std::uint8_t tail2) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::array<std::uint8_t, 3> bytes{digest[0], tail1, tail2};
    std::string code;
    for (auto byte : bytes) {
        code.push_back(hex[byte >> 4U]);
        code.push_back(hex[byte & 0x0fU]);
    }
    return code;
}

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
                     ("androidtvremote-pairing-" + std::to_string(nonce));
    std::filesystem::create_directories(dir);
    const auto certPath = dir / "cert.pem";
    const auto keyPath = dir / "key.pem";

    AndroidTvRemote generator({
        .clientName = "pairing-test",
        .certificateFile = certPath,
        .privateKeyFile = keyPath,
        .host = "127.0.0.1",
        .autoReconnect = false,
    });
    assert(generator.generateCertificateIfMissing());

    auto cert = loadCert(certPath);
    constexpr std::uint8_t tail1 = 0x12;
    constexpr std::uint8_t tail2 = 0x34;
    const auto expected = expectedSecret(cert.get(), cert.get(), tail1, tail2);
    const auto code = hexCode(expected, tail1, tail2);

    asio::io_context io;
    asio::ssl::context serverContext(asio::ssl::context::tls_server);
    serverContext.use_certificate_chain_file(certPath.string());
    serverContext.use_private_key_file(keyPath.string(), asio::ssl::context::pem);
    serverContext.load_verify_file(certPath.string());
    serverContext.set_verify_mode(
        asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor.local_endpoint().port();

    std::promise<void> serverDone;
    auto done = serverDone.get_future();
    std::thread server([&] {
        asio::ssl::stream<tcp::socket> stream(io, serverContext);
        acceptor.accept(stream.next_layer());
        stream.handshake(asio::ssl::stream_base::server);

        auto request = pwire::receiveFrame(stream);
        assert(!pwire::findBytes(request, 10).empty());
        pwire::sendFrame(stream, pwire::outer(11));

        auto options = pwire::receiveFrame(stream);
        assert(!pwire::findBytes(options, 20).empty());
        pwire::sendFrame(stream, pwire::outer(20));

        auto configuration = pwire::receiveFrame(stream);
        assert(!pwire::findBytes(configuration, 30).empty());
        pwire::sendFrame(stream, pwire::outer(31));

        auto secretOuter = pwire::receiveFrame(stream);
        auto secretMessage = pwire::findBytes(secretOuter, 40);
        assert(!secretMessage.empty());
        auto secret = pwire::findBytes(secretMessage, 1);
        assert(secret.size() == expected.size());
        assert(std::equal(secret.begin(), secret.end(), expected.begin()));

        std::vector<std::uint8_t> ack;
        pwire::bytesField(ack, 1, expected);
        pwire::sendFrame(stream, pwire::outer(41, ack));
        serverDone.set_value();

        boost::system::error_code ignored;
        stream.shutdown(ignored);
        stream.next_layer().close(ignored);
    });

    AndroidTvRemote remote({
        .clientName = "pairing-test",
        .certificateFile = certPath,
        .privateKeyFile = keyPath,
        .host = "127.0.0.1",
        .pairingPort = port,
        .autoReconnect = false,
    });
    remote.startPairing();
    auto invalidCode = code;
    invalidCode.front() = invalidCode.front() == '0' ? '1' : '0';
    bool rejected = false;
    try {
        remote.finishPairing(invalidCode);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    // A locally rejected typo must not destroy the live pairing session.
    remote.finishPairing(code);
    done.wait();
    server.join();

    std::filesystem::remove_all(dir);
    std::cout << "androidtvremote TLS pairing integration test: OK\n";
    return 0;
}
