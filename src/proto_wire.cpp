#include "proto_wire.hpp"

#include <limits>

namespace androidtvremote::wire {

void Writer::varint(std::uint64_t value) {
    while (value >= 0x80) {
        data_.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    data_.push_back(static_cast<std::uint8_t>(value));
}

void Writer::key(std::uint32_t fieldNumber, WireType type) {
    varint((static_cast<std::uint64_t>(fieldNumber) << 3U) |
           static_cast<std::uint8_t>(type));
}

void Writer::varintField(std::uint32_t fieldNumber, std::uint64_t value) {
    key(fieldNumber, WireType::Varint);
    varint(value);
}

void Writer::bytesField(std::uint32_t fieldNumber, std::span<const std::uint8_t> value) {
    key(fieldNumber, WireType::LengthDelimited);
    varint(value.size());
    data_.insert(data_.end(), value.begin(), value.end());
}

void Writer::stringField(std::uint32_t fieldNumber, std::string_view value) {
    bytesField(
        fieldNumber,
        {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}

void Writer::messageField(std::uint32_t fieldNumber, const Writer& nested) {
    bytesField(fieldNumber, nested.data());
}

bool Reader::readVarint(std::uint64_t& value) {
    value = 0;
    unsigned shift = 0;
    while (offset_ < bytes_.size() && shift <= 63) {
        const auto byte = bytes_[offset_++];
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            return true;
        }
        shift += 7;
    }
    ok_ = false;
    return false;
}

bool Reader::next(Field& field) {
    if (!ok_ || offset_ >= bytes_.size()) {
        return false;
    }

    std::uint64_t keyValue = 0;
    if (!readVarint(keyValue)) {
        return false;
    }
    field.number = static_cast<std::uint32_t>(keyValue >> 3U);
    field.type = static_cast<WireType>(keyValue & 0x07U);
    field.varint = 0;
    field.bytes = {};
    if (field.number == 0) {
        ok_ = false;
        return false;
    }

    switch (field.type) {
    case WireType::Varint:
        return readVarint(field.varint);
    case WireType::Fixed64:
        if (bytes_.size() - offset_ < 8) {
            ok_ = false;
            return false;
        }
        field.bytes = bytes_.subspan(offset_, 8);
        offset_ += 8;
        return true;
    case WireType::LengthDelimited: {
        std::uint64_t size = 0;
        if (!readVarint(size) || size > bytes_.size() - offset_) {
            ok_ = false;
            return false;
        }
        field.bytes = bytes_.subspan(offset_, static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return true;
    }
    case WireType::Fixed32:
        if (bytes_.size() - offset_ < 4) {
            ok_ = false;
            return false;
        }
        field.bytes = bytes_.subspan(offset_, 4);
        offset_ += 4;
        return true;
    default:
        ok_ = false;
        return false;
    }
}

std::optional<std::uint64_t> findVarint(
    std::span<const std::uint8_t> message,
    std::uint32_t fieldNumber) {
    Reader reader(message);
    Field field;
    while (reader.next(field)) {
        if (field.number == fieldNumber && field.type == WireType::Varint) {
            return field.varint;
        }
    }
    return std::nullopt;
}

std::optional<std::span<const std::uint8_t>> findBytes(
    std::span<const std::uint8_t> message,
    std::uint32_t fieldNumber) {
    Reader reader(message);
    Field field;
    while (reader.next(field)) {
        if (field.number == fieldNumber && field.type == WireType::LengthDelimited) {
            return field.bytes;
        }
    }
    return std::nullopt;
}

bool hasField(std::span<const std::uint8_t> message, std::uint32_t fieldNumber) {
    Reader reader(message);
    Field field;
    while (reader.next(field)) {
        if (field.number == fieldNumber) {
            return true;
        }
    }
    return false;
}

std::vector<std::uint8_t> frame(std::span<const std::uint8_t> payload) {
    // Encode the length prefix directly.
    std::vector<std::uint8_t> result;
    std::uint64_t value = payload.size();
    while (value >= 0x80) {
        result.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    result.push_back(static_cast<std::uint8_t>(value));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

bool decodeVarintPrefix(
    std::span<const std::uint8_t> data,
    std::uint64_t& value,
    std::size_t& prefixLength) {
    value = 0;
    prefixLength = 0;
    unsigned shift = 0;
    for (const auto byte : data) {
        if (shift > 63) {
            return false;
        }
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        ++prefixLength;
        if ((byte & 0x80U) == 0) {
            return true;
        }
        shift += 7;
    }
    prefixLength = 0;
    return false;
}

}  // namespace androidtvremote::wire
