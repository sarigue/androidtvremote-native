#include "pairing.hpp"

#include "certificate.hpp"
#include "proto_wire.hpp"
#include "protocol_messages.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace androidtvremote {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

class SyncTlsConnection {
public:
    SyncTlsConnection(
        const std::filesystem::path& certificateFile,
        const std::filesystem::path& privateKeyFile)
        : context_(asio::ssl::context::tls_client), stream_(io_, context_) {
        context_.set_verify_mode(asio::ssl::verify_none);
        crypto::loadClientIdentity(context_.native_handle(), certificateFile, privateKeyFile);
    }

    void connect(const std::string& host, std::uint16_t port) {
        tcp::resolver resolver(io_);
        const auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(stream_.next_layer(), endpoints);
        stream_.handshake(asio::ssl::stream_base::client);
    }

    void close() noexcept {
        boost::system::error_code ignored;
        stream_.shutdown(ignored);
        stream_.next_layer().close(ignored);
    }

    ~SyncTlsConnection() { close(); }

    void send(std::span<const std::uint8_t> payload) {
        const auto framed = wire::frame(payload);
        asio::write(stream_, asio::buffer(framed));
    }

    std::vector<std::uint8_t> receive() {
        std::uint64_t length = 0;
        unsigned shift = 0;
        while (true) {
            std::uint8_t byte = 0;
            asio::read(stream_, asio::buffer(&byte, 1));
            length |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0) break;
            shift += 7;
            if (shift > 63) throw std::runtime_error("invalid protobuf frame length");
        }
        if (length > 4 * 1024 * 1024) {
            throw std::runtime_error("protobuf frame exceeds safety limit");
        }
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
        if (!payload.empty()) {
            asio::read(stream_, asio::buffer(payload));
        }
        return payload;
    }

    X509* peerCertificate() {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        return SSL_get1_peer_certificate(stream_.native_handle());
#else
        return SSL_get_peer_certificate(stream_.native_handle());
#endif
    }

private:
    asio::io_context io_;
    asio::ssl::context context_;
    asio::ssl::stream<tcp::socket> stream_;
};

std::unique_ptr<X509, decltype(&X509_free)> loadCertificate(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to open client certificate");
    const std::string pem{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(
        BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    if (!bio) throw std::runtime_error("unable to allocate client certificate BIO");
    X509* raw = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
    if (!raw) throw std::runtime_error("unable to read client certificate PEM");
    return {raw, X509_free};
}

void requirePairingStatusOk(std::span<const std::uint8_t> message) {
    const auto status = protocol::pairingStatus(message);
    if (!status || *status != 200) {
        throw std::runtime_error("Android TV pairing protocol returned an error status");
    }
}

}  // namespace

class PairingSession::Impl {
public:
    Impl(
        std::string clientName,
        std::filesystem::path certificateFile,
        std::filesystem::path privateKeyFile,
        std::string host,
        std::uint16_t port)
        : clientName_(std::move(clientName)),
          certificateFile_(std::move(certificateFile)),
          privateKeyFile_(std::move(privateKeyFile)),
          host_(std::move(host)),
          port_(port) {}

    void start() {
        connection_ = std::make_unique<SyncTlsConnection>(certificateFile_, privateKeyFile_);
        connection_->connect(host_, port_);
        connection_->send(protocol::pairingRequest(clientName_));

        bool configured = false;
        for (int step = 0; step < 8 && !configured; ++step) {
            const auto message = connection_->receive();
            requirePairingStatusOk(message);
            if (protocol::hasPairingRequestAck(message)) {
                connection_->send(protocol::pairingOptions());
            } else if (protocol::hasPairingOptions(message)) {
                connection_->send(protocol::pairingConfiguration());
            } else if (protocol::hasConfigurationAck(message)) {
                configured = true;
            }
        }
        if (!configured) {
            connection_.reset();
            throw std::runtime_error("pairing handshake did not reach configuration acknowledgement");
        }
    }

    void finish(std::string_view code) {
        if (!connection_) {
            throw std::runtime_error("finishPairing called before startPairing");
        }
        auto clientCert = loadCertificate(certificateFile_);
        std::unique_ptr<X509, decltype(&X509_free)> serverCert(connection_->peerCertificate(), X509_free);
        if (!serverCert) {
            throw std::runtime_error("Android TV did not present a TLS certificate");
        }
        const auto secret = crypto::pairingSecret(clientCert.get(), serverCert.get(), code);
        connection_->send(protocol::pairingSecret(secret));

        bool acknowledged = false;
        for (int step = 0; step < 4 && !acknowledged; ++step) {
            const auto message = connection_->receive();
            requirePairingStatusOk(message);
            acknowledged = protocol::hasSecretAck(message);
        }
        connection_.reset();
        if (!acknowledged) {
            throw std::runtime_error("Android TV did not acknowledge the pairing secret");
        }
    }

    DeviceIdentity getNameAndMac() {
        SyncTlsConnection connection(certificateFile_, privateKeyFile_);
        connection.connect(host_, port_);
        std::unique_ptr<X509, decltype(&X509_free)> certificate(connection.peerCertificate(), X509_free);
        if (!certificate) throw std::runtime_error("Android TV did not present a TLS certificate");
        return crypto::parseDeviceIdentity(certificate.get());
    }

private:
    std::string clientName_;
    std::filesystem::path certificateFile_;
    std::filesystem::path privateKeyFile_;
    std::string host_;
    std::uint16_t port_;
    std::unique_ptr<SyncTlsConnection> connection_;
};

PairingSession::PairingSession(
    std::string clientName,
    std::filesystem::path certificateFile,
    std::filesystem::path privateKeyFile,
    std::string host,
    std::uint16_t port)
    : impl_(std::make_unique<Impl>(
          std::move(clientName),
          std::move(certificateFile),
          std::move(privateKeyFile),
          std::move(host),
          port)) {}

PairingSession::~PairingSession() = default;
void PairingSession::start() { impl_->start(); }
void PairingSession::finish(std::string_view code) { impl_->finish(code); }
DeviceIdentity PairingSession::getNameAndMac() { return impl_->getNameAndMac(); }

}  // namespace androidtvremote
