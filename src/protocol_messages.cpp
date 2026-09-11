#include "protocol_messages.hpp"

#include "proto_wire.hpp"

#include <algorithm>

namespace androidtvremote::protocol {
namespace {

wire::Writer pairingOuter(std::uint32_t fieldNumber, const wire::Writer& inner) {
    wire::Writer outer;
    outer.varintField(1, 2);    // protocol_version
    outer.varintField(2, 200);  // STATUS_OK
    outer.messageField(fieldNumber, inner);
    return outer;
}

std::string asString(std::span<const std::uint8_t> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::int32_t asI32(std::optional<std::uint64_t> value) {
    return value ? static_cast<std::int32_t>(*value) : 0;
}

std::uint32_t asU32(std::optional<std::uint64_t> value) {
    return value ? static_cast<std::uint32_t>(*value) : 0;
}

}  // namespace

std::vector<std::uint8_t> pairingRequest(std::string_view clientName) {
    wire::Writer request;
    request.stringField(1, "atvremote");
    request.stringField(2, clientName);
    return std::move(pairingOuter(10, request)).take();
}

std::vector<std::uint8_t> pairingOptions() {
    wire::Writer encoding;
    encoding.varintField(1, 3);  // HEXADECIMAL
    encoding.varintField(2, 6);  // symbol length

    wire::Writer options;
    options.messageField(1, encoding);  // input_encodings
    options.varintField(3, 1);          // ROLE_TYPE_INPUT
    return std::move(pairingOuter(20, options)).take();
}

std::vector<std::uint8_t> pairingConfiguration() {
    wire::Writer encoding;
    encoding.varintField(1, 3);
    encoding.varintField(2, 6);

    wire::Writer configuration;
    configuration.messageField(1, encoding);
    configuration.varintField(2, 1);  // client role INPUT
    return std::move(pairingOuter(30, configuration)).take();
}

std::vector<std::uint8_t> pairingSecret(std::span<const std::uint8_t> secretBytes) {
    wire::Writer secret;
    secret.bytesField(1, secretBytes);
    return std::move(pairingOuter(40, secret)).take();
}

std::optional<std::uint64_t> pairingStatus(std::span<const std::uint8_t> message) {
    return wire::findVarint(message, 2);
}

bool hasPairingRequestAck(std::span<const std::uint8_t> message) { return wire::hasField(message, 11); }
bool hasPairingOptions(std::span<const std::uint8_t> message) { return wire::hasField(message, 20); }
bool hasConfigurationAck(std::span<const std::uint8_t> message) { return wire::hasField(message, 31); }
bool hasSecretAck(std::span<const std::uint8_t> message) { return wire::hasField(message, 41); }

std::vector<std::uint8_t> remoteConfigureResponse(std::int32_t activeFeatures) {
    wire::Writer device;
    device.varintField(3, 1);
    device.stringField(4, "1");
    device.stringField(5, "atvremote");
    device.stringField(6, "1.0.0");

    wire::Writer configure;
    configure.varintField(1, static_cast<std::uint64_t>(activeFeatures));
    configure.messageField(2, device);

    wire::Writer remote;
    remote.messageField(1, configure);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteSetActiveResponse(std::int32_t activeFeatures) {
    wire::Writer active;
    active.varintField(1, static_cast<std::uint64_t>(activeFeatures));
    wire::Writer remote;
    remote.messageField(2, active);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remotePingResponse(std::int32_t value) {
    wire::Writer ping;
    ping.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)));
    wire::Writer remote;
    remote.messageField(9, ping);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteKey(std::int32_t keyCode, Direction direction) {
    wire::Writer key;
    key.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(keyCode)));
    key.varintField(2, static_cast<std::uint64_t>(direction));
    wire::Writer remote;
    remote.messageField(10, key);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteText(
    std::string_view text,
    std::int32_t imeCounter,
    std::int32_t fieldCounter) {
    const auto pos = static_cast<std::int32_t>(text.size()) - 1;

    wire::Writer imeObject;
    imeObject.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(pos)));
    imeObject.varintField(2, static_cast<std::uint64_t>(static_cast<std::uint32_t>(pos)));
    imeObject.stringField(3, text);

    wire::Writer editInfo;
    editInfo.varintField(1, 1);
    editInfo.messageField(2, imeObject);

    wire::Writer batch;
    batch.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(imeCounter)));
    batch.varintField(2, static_cast<std::uint64_t>(static_cast<std::uint32_t>(fieldCounter)));
    batch.messageField(3, editInfo);

    wire::Writer remote;
    remote.messageField(21, batch);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteAppLink(std::string_view appLink) {
    wire::Writer launch;
    launch.stringField(1, appLink);
    wire::Writer remote;
    remote.messageField(90, launch);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteVoiceBegin(std::int32_t sessionId) {
    wire::Writer begin;
    begin.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(sessionId)));
    wire::Writer remote;
    remote.messageField(30, begin);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteVoicePayload(
    std::int32_t sessionId,
    std::span<const std::uint8_t> samples) {
    wire::Writer payload;
    payload.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(sessionId)));
    payload.bytesField(2, samples);
    wire::Writer remote;
    remote.messageField(31, payload);
    return std::move(remote).take();
}

std::vector<std::uint8_t> remoteVoiceEnd(std::int32_t sessionId) {
    wire::Writer end;
    end.varintField(1, static_cast<std::uint64_t>(static_cast<std::uint32_t>(sessionId)));
    wire::Writer remote;
    remote.messageField(32, end);
    return std::move(remote).take();
}

RemoteIncoming parseRemote(std::span<const std::uint8_t> message) {
    RemoteIncoming result;
    wire::Reader reader(message);
    wire::Field field;
    while (reader.next(field)) {
        if (field.type != wire::WireType::LengthDelimited) {
            continue;
        }

        switch (field.number) {
        case 1: {  // remote_configure
            result.kind = RemoteIncoming::Kind::Configure;
            result.supportedFeatures = asI32(wire::findVarint(field.bytes, 1));
            if (const auto info = wire::findBytes(field.bytes, 2)) {
                if (const auto model = wire::findBytes(*info, 1)) {
                    result.deviceInfo.model = asString(*model);
                }
                if (const auto vendor = wire::findBytes(*info, 2)) {
                    result.deviceInfo.manufacturer = asString(*vendor);
                }
                if (const auto version = wire::findBytes(*info, 6)) {
                    result.deviceInfo.softwareVersion = asString(*version);
                }
            }
            return result;
        }
        case 2:
            result.kind = RemoteIncoming::Kind::SetActive;
            return result;
        case 3:
            result.kind = RemoteIncoming::Kind::Error;
            return result;
        case 8:
            result.kind = RemoteIncoming::Kind::Ping;
            result.pingValue = asI32(wire::findVarint(field.bytes, 1));
            return result;
        case 20: {  // IME key inject
            result.kind = RemoteIncoming::Kind::ImeKeyInject;
            if (const auto appInfo = wire::findBytes(field.bytes, 1)) {
                if (const auto appPackage = wire::findBytes(*appInfo, 12)) {
                    result.currentApp = asString(*appPackage);
                }
            }
            return result;
        }
        case 21:
            result.kind = RemoteIncoming::Kind::ImeBatchEdit;
            result.imeCounter = asI32(wire::findVarint(field.bytes, 1));
            result.fieldCounter = asI32(wire::findVarint(field.bytes, 2));
            return result;
        case 30:
            result.kind = RemoteIncoming::Kind::VoiceBegin;
            result.voiceSessionId = asI32(wire::findVarint(field.bytes, 1));
            return result;
        case 40:
            result.kind = RemoteIncoming::Kind::Start;
            result.started = wire::findVarint(field.bytes, 1).value_or(0) != 0;
            return result;
        case 50:
            result.kind = RemoteIncoming::Kind::Volume;
            result.volume.maximum = asU32(wire::findVarint(field.bytes, 6));
            result.volume.level = asU32(wire::findVarint(field.bytes, 7));
            result.volume.muted = wire::findVarint(field.bytes, 8).value_or(0) != 0;
            return result;
        default:
            break;
        }
    }
    return result;
}

}  // namespace androidtvremote::protocol
