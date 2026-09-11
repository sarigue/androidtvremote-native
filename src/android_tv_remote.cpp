#include "androidtvremote/android_tv_remote.hpp"
#include "androidtvremote/key_codes.hpp"

#include "certificate.hpp"
#include "pairing.hpp"
#include "proto_wire.hpp"
#include "protocol_messages.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace androidtvremote {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

namespace {

constexpr std::size_t MaxFrameSize = 4 * 1024 * 1024;
constexpr std::size_t VoiceChunkSize = 20 * 1024;
constexpr std::size_t VoiceMinimumChunkSize = 8 * 1024;
constexpr auto VoiceStartTimeout = 2s;
constexpr auto IdleTimeout = 16s;

std::string upperKeyName(std::string_view input) {
    std::string value(input);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (value.rfind("KEYCODE_", 0) == 0) value.erase(0, 8);
    return value;
}

}  // namespace

std::optional<std::int32_t> keyCodeFromName(std::string_view name) {
    static const std::unordered_map<std::string, std::int32_t> codes = {
        {"HOME", 3},
        {"BACK", 4},
        {"0", 7},
        {"1", 8},
        {"2", 9},
        {"3", 10},
        {"4", 11},
        {"5", 12},
        {"6", 13},
        {"7", 14},
        {"8", 15},
        {"9", 16},
        {"DPAD_UP", 19},
        {"DPAD_DOWN", 20},
        {"DPAD_LEFT", 21},
        {"DPAD_RIGHT", 22},
        {"DPAD_CENTER", 23},
        {"VOLUME_UP", 24},
        {"VOLUME_DOWN", 25},
        {"POWER", 26},
        {"ENTER", 66},
        {"DEL", 67},
        {"SEARCH", 84},
        {"MEDIA_PLAY_PAUSE", 85},
        {"PAGE_UP", 92},
        {"PAGE_DOWN", 93},
        {"MUTE", 91},
        {"VOLUME_MUTE", 164},
        {"CHANNEL_UP", 166},
        {"CHANNEL_DOWN", 167},
    };
    const auto key = upperKeyName(name);
    const auto found = codes.find(key);
    if (found == codes.end()) return std::nullopt;
    return found->second;
}

class AndroidTvRemote::Impl {
public:
    explicit Impl(Options options)
        : options_(std::move(options)),
          work_(asio::make_work_guard(io_)),
          resolver_(io_),
          reconnectTimer_(io_),
          idleTimer_(io_),
          voiceTimer_(io_) {
        activeFeatures_ = protocol::FeaturePing | protocol::FeatureKey | protocol::FeaturePower |
                          protocol::FeatureVolume | protocol::FeatureAppLink;
        if (options_.enableIme) activeFeatures_ |= protocol::FeatureIme;
        if (options_.enableVoice) activeFeatures_ |= protocol::FeatureVoice;
        ioThread_ = std::thread([this] { io_.run(); });
    }

    ~Impl() {
        shuttingDown_.store(true);
        asio::post(io_, [this] {
            userDisconnect_ = true;
            closeStream();
            reconnectTimer_.cancel();
            idleTimer_.cancel();
            voiceTimer_.cancel();
            work_.reset();
        });
        io_.stop();
        if (ioThread_.joinable()) ioThread_.join();
    }

    void setCallbacks(Callbacks callbacks) {
        std::lock_guard lock(callbackMutex_);
        callbacks_ = std::move(callbacks);
    }

    bool generateCertificateIfMissing() {
        return crypto::generateCertificateIfMissing(
            options_.certificateFile,
            options_.privateKeyFile,
            options_.clientName);
    }

    void startPairing() {
        disconnectAndWait();
        auto session = std::make_unique<PairingSession>(
            options_.clientName,
            options_.certificateFile,
            options_.privateKeyFile,
            options_.host,
            options_.pairingPort);
        session->start();
        std::lock_guard lock(pairingMutex_);
        pairing_ = std::move(session);
    }

    void finishPairing(std::string_view code) {
        std::lock_guard lock(pairingMutex_);
        if (!pairing_) throw std::runtime_error("finishPairing called before startPairing");
        // Keep the live pairing session when local validation rejects a mistyped
        // code, so the caller can retry without restarting the TV pairing flow.
        pairing_->finish(code);
        pairing_.reset();
    }

    DeviceIdentity getNameAndMac() {
        PairingSession session(
            options_.clientName,
            options_.certificateFile,
            options_.privateKeyFile,
            options_.host,
            options_.pairingPort);
        return session.getNameAndMac();
    }

    void connect() {
        asio::post(io_, [this] {
            userDisconnect_ = false;
            reconnectTimer_.cancel();
            reconnectDelay_ = 100ms;
            beginConnect(false);
        });
    }

    void disconnect() {
        asio::post(io_, [this] {
            userDisconnect_ = true;
            reconnectTimer_.cancel();
            voiceTimer_.cancel();
            idleTimer_.cancel();
            closeStream();
            setState(ConnectionState::Disconnected);
            emitAvailability(false);
        });
    }

    ConnectionState connectionState() const noexcept { return state_.load(); }

    void sendKey(std::int32_t key, Direction direction) {
        postMessage(protocol::remoteKey(key, direction));
    }

    void sendText(std::string text) {
        if (text.empty()) throw std::invalid_argument("text cannot be empty");
        asio::post(io_, [this, text = std::move(text)] {
            queueMessage(protocol::remoteText(text, imeCounter_, imeFieldCounter_));
        });
    }

    void launchApp(std::string appLink) {
        postMessage(protocol::remoteAppLink(appLink));
    }

    void startVoice() {
        asio::post(io_, [this] {
            if (!voiceEnabled_.load()) {
                emitError("voice is not enabled or not supported by the Android TV");
                return;
            }
            if (voiceStarting_ || voiceSessionId_) {
                emitError("voice session already in progress");
                return;
            }
            if (state_.load() != ConnectionState::Connected) {
                emitError("cannot start voice while disconnected");
                return;
            }
            voiceStarting_ = true;
            queueMessage(protocol::remoteKey(static_cast<std::int32_t>(KeyCode::Search), Direction::Short));
            voiceTimer_.expires_after(VoiceStartTimeout);
            voiceTimer_.async_wait([this](const boost::system::error_code& error) {
                if (!error && voiceStarting_) {
                    voiceStarting_ = false;
                    emitError("Android TV did not start a voice session within 2 seconds");
                }
            });
        });
    }

    void sendVoiceData(std::span<const std::uint8_t> pcm) {
        std::vector<std::uint8_t> data(pcm.begin(), pcm.end());
        asio::post(io_, [this, data = std::move(data)]() mutable {
            if (!voiceSessionId_) return;
            for (std::size_t offset = 0; offset < data.size(); offset += VoiceChunkSize) {
                // Parenthesized to avoid expansion of Windows' min macro.
                const auto length = (std::min)(VoiceChunkSize, data.size() - offset);
                std::vector<std::uint8_t> chunk(
                    data.begin() + static_cast<std::ptrdiff_t>(offset),
                    data.begin() + static_cast<std::ptrdiff_t>(offset + length));
                if (chunk.size() < VoiceMinimumChunkSize) {
                    chunk.resize(VoiceMinimumChunkSize, 0);
                }
                queueMessage(protocol::remoteVoicePayload(*voiceSessionId_, chunk));
            }
        });
    }

    void stopVoice() {
        asio::post(io_, [this] {
            voiceTimer_.cancel();
            voiceStarting_ = false;
            if (voiceSessionId_) {
                queueMessage(protocol::remoteVoiceEnd(*voiceSessionId_));
                voiceSessionId_.reset();
                emitVoiceStopped();
            }
        });
    }

    bool isVoiceEnabled() const noexcept { return voiceEnabled_.load(); }
    bool isOn() const noexcept { return isOn_.load(); }

    std::string currentApp() const {
        std::lock_guard lock(stateDataMutex_);
        return currentApp_;
    }

    DeviceInfo deviceInfo() const {
        std::lock_guard lock(stateDataMutex_);
        return deviceInfo_;
    }

    VolumeInfo volumeInfo() const {
        std::lock_guard lock(stateDataMutex_);
        return volumeInfo_;
    }

private:
    using Stream = asio::ssl::stream<tcp::socket>;

    template <typename F>
    void withCallbacks(F&& function) {
        Callbacks copy;
        {
            std::lock_guard lock(callbackMutex_);
            copy = callbacks_;
        }
        function(copy);
    }

    void emitError(const std::string& message) {
        withCallbacks([&](const Callbacks& callbacks) {
            if (callbacks.onError) callbacks.onError(message);
        });
    }

    void emitAvailability(bool available) {
        withCallbacks([&](const Callbacks& callbacks) {
            if (callbacks.onAvailabilityChanged) callbacks.onAvailabilityChanged(available);
        });
    }

    void emitVoiceStopped() {
        withCallbacks([&](const Callbacks& callbacks) {
            if (callbacks.onVoiceStopped) callbacks.onVoiceStopped();
        });
    }

    void setState(ConnectionState state) {
        state_.store(state);
        withCallbacks([&](const Callbacks& callbacks) {
            if (callbacks.onConnectionState) callbacks.onConnectionState(state);
        });
    }

    void disconnectAndWait() {
        std::promise<void> done;
        auto future = done.get_future();
        asio::post(io_, [this, &done] {
            userDisconnect_ = true;
            reconnectTimer_.cancel();
            idleTimer_.cancel();
            voiceTimer_.cancel();
            closeStream();
            setState(ConnectionState::Disconnected);
            done.set_value();
        });
        future.get();
    }

    void ensureSslContext() {
        if (sslContext_) return;
        sslContext_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
        sslContext_->set_verify_mode(asio::ssl::verify_none);
        crypto::loadClientIdentity(
            sslContext_->native_handle(), options_.certificateFile, options_.privateKeyFile);
    }

    void beginConnect(bool reconnecting) {
        if (shuttingDown_.load() || userDisconnect_) return;
        closeStream();
        const auto generation = generation_;
        try {
            ensureSslContext();
        } catch (const std::exception& error) {
            emitError(std::string("unable to load TLS certificate/key: ") + error.what());
            setState(ConnectionState::Disconnected);
            return;
        }

        setState(reconnecting ? ConnectionState::Reconnecting : ConnectionState::Connecting);
        stream_ = std::make_unique<Stream>(io_, *sslContext_);
        resolver_.async_resolve(
            options_.host,
            std::to_string(options_.apiPort),
            [this, generation](const boost::system::error_code& error, const tcp::resolver::results_type& endpoints) {
                if (generation != generation_) return;
                if (error) return connectionFailed("DNS/host resolution failed", error, generation);
                asio::async_connect(
                    stream_->next_layer(),
                    endpoints,
                    [this, generation](const boost::system::error_code& connectError, const tcp::endpoint&) {
                        if (generation != generation_) return;
                        if (connectError) return connectionFailed("TCP connection failed", connectError, generation);
                        stream_->async_handshake(
                            asio::ssl::stream_base::client,
                            [this, generation](const boost::system::error_code& handshakeError) {
                                if (generation != generation_) return;
                                if (handshakeError) return authenticationFailed(handshakeError, generation);
                                reconnectDelay_ = 100ms;
                                beginReadLength(generation);
                                resetIdleTimer();
                            });
                    });
            });
    }


    void authenticationFailed(
        const boost::system::error_code& error,
        std::uint64_t generation) {
        if (generation != generation_ || userDisconnect_ || shuttingDown_.load()) return;
        emitError(std::string("TLS authentication failed; pairing is required: ") + error.message());
        const bool wasConnected = state_.load() == ConnectionState::Connected;
        closeStream();
        setState(ConnectionState::Disconnected);
        if (wasConnected) emitAvailability(false);
        withCallbacks([](const Callbacks& callbacks) {
            if (callbacks.onAuthenticationRequired) callbacks.onAuthenticationRequired();
        });
    }

    void connectionFailed(
        std::string_view prefix,
        const boost::system::error_code& error,
        std::uint64_t generation) {
        if (generation != generation_ || userDisconnect_ || shuttingDown_.load()) return;
        emitError(std::string(prefix) + ": " + error.message());
        handleConnectionLoss(generation);
    }

    void handleConnectionLoss(std::uint64_t generation) {
        if (generation != generation_) return;
        const bool wasConnected = state_.load() == ConnectionState::Connected;
        closeStream();
        if (wasConnected) emitAvailability(false);
        if (options_.autoReconnect && !userDisconnect_ && !shuttingDown_.load()) {
            setState(ConnectionState::Reconnecting);
            reconnectTimer_.expires_after(reconnectDelay_);
            reconnectTimer_.async_wait([this](const boost::system::error_code& error) {
                if (!error && !userDisconnect_) beginConnect(true);
            });
            reconnectDelay_ =
                (std::min)(reconnectDelay_ * 2, std::chrono::milliseconds{30000});
        } else {
            setState(ConnectionState::Disconnected);
        }
    }

    void closeStream() {
        ++generation_;
        voiceStarting_ = false;
        voiceSessionId_.reset();
        writeQueue_.clear();
        writeInProgress_ = false;
        readLength_ = 0;
        readShift_ = 0;
        if (stream_) {
            boost::system::error_code ignored;
            stream_->next_layer().cancel(ignored);
            stream_->next_layer().shutdown(tcp::socket::shutdown_both, ignored);
            stream_->next_layer().close(ignored);
            stream_.reset();
        }
    }

    void beginReadLength(std::uint64_t generation) {
        if (!stream_ || generation != generation_) return;
        readLength_ = 0;
        readShift_ = 0;
        readOneLengthByte(generation);
    }

    void readOneLengthByte(std::uint64_t generation) {
        if (!stream_ || generation != generation_) return;
        asio::async_read(
            *stream_,
            asio::buffer(&lengthByte_, 1),
            [this, generation](const boost::system::error_code& error, std::size_t) {
                if (generation != generation_) return;
                if (error) return connectionFailed("read failed", error, generation);
                readLength_ |= static_cast<std::uint64_t>(lengthByte_ & 0x7fU) << readShift_;
                if ((lengthByte_ & 0x80U) != 0) {
                    readShift_ += 7;
                    if (readShift_ > 63) {
                        emitError("invalid protobuf frame length");
                        return handleConnectionLoss(generation);
                    }
                    return readOneLengthByte(generation);
                }
                if (readLength_ > MaxFrameSize) {
                    emitError("protobuf frame exceeds 4 MiB safety limit");
                    return handleConnectionLoss(generation);
                }
                readBody_.assign(static_cast<std::size_t>(readLength_), 0);
                if (readBody_.empty()) {
                    handleMessage({});
                    return beginReadLength(generation);
                }
                asio::async_read(
                    *stream_,
                    asio::buffer(readBody_),
                    [this, generation](const boost::system::error_code& bodyError, std::size_t) {
                        if (generation != generation_) return;
                        if (bodyError) return connectionFailed("read failed", bodyError, generation);
                        resetIdleTimer();
                        handleMessage(readBody_);
                        beginReadLength(generation);
                    });
            });
    }

    void handleMessage(std::span<const std::uint8_t> message) {
        const auto incoming = protocol::parseRemote(message);
        switch (incoming.kind) {
        case protocol::RemoteIncoming::Kind::Configure: {
            supportedFeatures_ = incoming.supportedFeatures;
            activeFeatures_ &= supportedFeatures_;
            voiceEnabled_.store((activeFeatures_ & protocol::FeatureVoice) != 0);
            {
                std::lock_guard lock(stateDataMutex_);
                deviceInfo_ = incoming.deviceInfo;
            }
            withCallbacks([&](const Callbacks& callbacks) {
                if (callbacks.onDeviceInfoChanged) callbacks.onDeviceInfoChanged(incoming.deviceInfo);
            });
            queueMessage(protocol::remoteConfigureResponse(activeFeatures_));
            break;
        }
        case protocol::RemoteIncoming::Kind::SetActive:
            queueMessage(protocol::remoteSetActiveResponse(activeFeatures_));
            break;
        case protocol::RemoteIncoming::Kind::ImeKeyInject:
            {
                std::lock_guard lock(stateDataMutex_);
                currentApp_ = incoming.currentApp;
            }
            withCallbacks([&](const Callbacks& callbacks) {
                if (callbacks.onCurrentAppChanged) callbacks.onCurrentAppChanged(incoming.currentApp);
            });
            break;
        case protocol::RemoteIncoming::Kind::ImeBatchEdit:
            imeCounter_ = incoming.imeCounter;
            imeFieldCounter_ = incoming.fieldCounter;
            break;
        case protocol::RemoteIncoming::Kind::Volume:
            {
                std::lock_guard lock(stateDataMutex_);
                volumeInfo_ = incoming.volume;
            }
            withCallbacks([&](const Callbacks& callbacks) {
                if (callbacks.onVolumeChanged) callbacks.onVolumeChanged(incoming.volume);
            });
            break;
        case protocol::RemoteIncoming::Kind::Start:
            isOn_.store(incoming.started);
            setState(ConnectionState::Connected);
            emitAvailability(true);
            withCallbacks([&](const Callbacks& callbacks) {
                if (callbacks.onPowerChanged) callbacks.onPowerChanged(incoming.started);
            });
            break;
        case protocol::RemoteIncoming::Kind::Ping:
            queueMessage(protocol::remotePingResponse(incoming.pingValue));
            break;
        case protocol::RemoteIncoming::Kind::Error:
            emitError("Android TV reported a remote protocol error");
            break;
        case protocol::RemoteIncoming::Kind::VoiceBegin:
            if (voiceStarting_) {
                voiceStarting_ = false;
                voiceTimer_.cancel();
                voiceSessionId_ = incoming.voiceSessionId;
                queueMessage(protocol::remoteVoiceBegin(incoming.voiceSessionId));
                withCallbacks([&](const Callbacks& callbacks) {
                    if (callbacks.onVoiceStarted) callbacks.onVoiceStarted(incoming.voiceSessionId);
                });
            }
            break;
        case protocol::RemoteIncoming::Kind::Unknown:
            break;
        }
    }

    void postMessage(std::vector<std::uint8_t> payload) {
        asio::post(io_, [this, payload = std::move(payload)]() mutable {
            queueMessage(std::move(payload));
        });
    }

    void queueMessage(std::vector<std::uint8_t> payload) {
        if (!stream_) return;
        resetIdleTimer();
        writeQueue_.push_back(wire::frame(payload));
        if (!writeInProgress_) beginWrite();
    }

    void beginWrite() {
        if (!stream_ || writeQueue_.empty()) {
            writeInProgress_ = false;
            return;
        }
        writeInProgress_ = true;
        const auto generation = generation_;
        asio::async_write(
            *stream_,
            asio::buffer(writeQueue_.front()),
            [this, generation](const boost::system::error_code& error, std::size_t) {
                if (generation != generation_) return;
                if (error) {
                    writeInProgress_ = false;
                    return connectionFailed("write failed", error, generation);
                }
                writeQueue_.pop_front();
                beginWrite();
            });
    }

    void resetIdleTimer() {
        const auto generation = generation_;
        idleTimer_.expires_after(IdleTimeout);
        idleTimer_.async_wait([this, generation](const boost::system::error_code& error) {
            if (!error && !userDisconnect_ && generation == generation_) {
                emitError("closing idle Android TV connection after 16 seconds without traffic");
                handleConnectionLoss(generation);
            }
        });
    }

    Options options_;
    mutable std::mutex callbackMutex_;
    Callbacks callbacks_;
    mutable std::mutex stateDataMutex_;
    DeviceInfo deviceInfo_;
    VolumeInfo volumeInfo_;
    std::string currentApp_;
    std::atomic<bool> isOn_{false};
    std::atomic<bool> voiceEnabled_{false};
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    std::atomic<bool> shuttingDown_{false};

    mutable std::mutex pairingMutex_;
    std::unique_ptr<PairingSession> pairing_;

    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    tcp::resolver resolver_;
    std::unique_ptr<asio::ssl::context> sslContext_;
    std::unique_ptr<Stream> stream_;
    asio::steady_timer reconnectTimer_;
    asio::steady_timer idleTimer_;
    asio::steady_timer voiceTimer_;
    std::thread ioThread_;

    bool userDisconnect_ = true;
    std::chrono::milliseconds reconnectDelay_{100};
    std::int32_t activeFeatures_ = 0;
    std::int32_t supportedFeatures_ = 0;
    std::int32_t imeCounter_ = 0;
    std::int32_t imeFieldCounter_ = 0;
    bool voiceStarting_ = false;
    std::optional<std::int32_t> voiceSessionId_;
    std::uint64_t generation_ = 0;

    std::uint8_t lengthByte_ = 0;
    std::uint64_t readLength_ = 0;
    unsigned readShift_ = 0;
    std::vector<std::uint8_t> readBody_;
    std::deque<std::vector<std::uint8_t>> writeQueue_;
    bool writeInProgress_ = false;
};

AndroidTvRemote::AndroidTvRemote(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
AndroidTvRemote::~AndroidTvRemote() = default;
AndroidTvRemote::AndroidTvRemote(AndroidTvRemote&&) noexcept = default;
AndroidTvRemote& AndroidTvRemote::operator=(AndroidTvRemote&&) noexcept = default;

void AndroidTvRemote::setCallbacks(Callbacks callbacks) { impl_->setCallbacks(std::move(callbacks)); }
bool AndroidTvRemote::generateCertificateIfMissing() { return impl_->generateCertificateIfMissing(); }
void AndroidTvRemote::startPairing() { impl_->startPairing(); }
void AndroidTvRemote::finishPairing(std::string_view code) { impl_->finishPairing(code); }
DeviceIdentity AndroidTvRemote::getNameAndMac() { return impl_->getNameAndMac(); }
void AndroidTvRemote::connect() { impl_->connect(); }
void AndroidTvRemote::disconnect() { impl_->disconnect(); }
ConnectionState AndroidTvRemote::connectionState() const noexcept { return impl_->connectionState(); }
void AndroidTvRemote::sendKey(KeyCode key, Direction direction) {
    impl_->sendKey(static_cast<std::int32_t>(key), direction);
}
void AndroidTvRemote::sendKey(std::int32_t key, Direction direction) { impl_->sendKey(key, direction); }
void AndroidTvRemote::sendKey(std::string_view keyName, Direction direction) {
    const auto key = keyCodeFromName(keyName);
    if (!key) throw std::invalid_argument("unknown Android key name: " + std::string(keyName));
    impl_->sendKey(*key, direction);
}
void AndroidTvRemote::sendText(std::string text) { impl_->sendText(std::move(text)); }
void AndroidTvRemote::launchApp(std::string appLink) { impl_->launchApp(std::move(appLink)); }
void AndroidTvRemote::startVoice() { impl_->startVoice(); }
void AndroidTvRemote::sendVoiceData(std::span<const std::byte> pcm) {
    impl_->sendVoiceData({reinterpret_cast<const std::uint8_t*>(pcm.data()), pcm.size()});
}
void AndroidTvRemote::sendVoiceData(std::span<const std::uint8_t> pcm) { impl_->sendVoiceData(pcm); }
void AndroidTvRemote::stopVoice() { impl_->stopVoice(); }
bool AndroidTvRemote::isVoiceEnabled() const noexcept { return impl_->isVoiceEnabled(); }
bool AndroidTvRemote::isOn() const noexcept { return impl_->isOn(); }
std::string AndroidTvRemote::currentApp() const { return impl_->currentApp(); }
DeviceInfo AndroidTvRemote::deviceInfo() const { return impl_->deviceInfo(); }
VolumeInfo AndroidTvRemote::volumeInfo() const { return impl_->volumeInfo(); }

}  // namespace androidtvremote
