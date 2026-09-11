#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace androidtvremote::wire {

enum class WireType : std::uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    Fixed32 = 5,
};

struct Field {
    std::uint32_t number = 0;
    WireType type = WireType::Varint;
    std::uint64_t varint = 0;
    std::span<const std::uint8_t> bytes{};
};

class Writer {
public:
    void varintField(std::uint32_t fieldNumber, std::uint64_t value);
    void bytesField(std::uint32_t fieldNumber, std::span<const std::uint8_t> value);
    void stringField(std::uint32_t fieldNumber, std::string_view value);
    void messageField(std::uint32_t fieldNumber, const Writer& nested);

    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return data_; }
    [[nodiscard]] std::vector<std::uint8_t> take() && { return std::move(data_); }

private:
    void key(std::uint32_t fieldNumber, WireType type);
    void varint(std::uint64_t value);
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool next(Field& field);
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    [[nodiscard]] bool readVarint(std::uint64_t& value);
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

[[nodiscard]] std::optional<std::uint64_t> findVarint(
    std::span<const std::uint8_t> message,
    std::uint32_t fieldNumber);
[[nodiscard]] std::optional<std::span<const std::uint8_t>> findBytes(
    std::span<const std::uint8_t> message,
    std::uint32_t fieldNumber);
[[nodiscard]] bool hasField(std::span<const std::uint8_t> message, std::uint32_t fieldNumber);
[[nodiscard]] std::vector<std::uint8_t> frame(std::span<const std::uint8_t> payload);
[[nodiscard]] bool decodeVarintPrefix(
    std::span<const std::uint8_t> data,
    std::uint64_t& value,
    std::size_t& prefixLength);

}  // namespace androidtvremote::wire
