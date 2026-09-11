#include "proto_wire.hpp"
#include "protocol_messages.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace androidtvremote;

int main() {
    {
        wire::Writer nested;
        nested.stringField(1, "hello");
        wire::Writer outer;
        outer.varintField(1, 300);
        outer.messageField(2, nested);
        assert(wire::findVarint(outer.data(), 1).value() == 300);
        auto bytes = wire::findBytes(outer.data(), 2);
        assert(bytes.has_value());
        auto text = wire::findBytes(*bytes, 1);
        assert(text.has_value());
        assert(std::string(reinterpret_cast<const char*>(text->data()), text->size()) == "hello");
    }

    {
        const auto key = protocol::remoteKey(26, Direction::Short);
        auto nested = wire::findBytes(key, 10);
        assert(nested.has_value());
        assert(wire::findVarint(*nested, 1).value() == 26);
        assert(wire::findVarint(*nested, 2).value() == 3);
    }

    {
        wire::Writer volume;
        volume.varintField(6, 100);
        volume.varintField(7, 42);
        volume.varintField(8, 1);
        wire::Writer remote;
        remote.messageField(50, volume);
        const auto parsed = protocol::parseRemote(remote.data());
        assert(parsed.kind == protocol::RemoteIncoming::Kind::Volume);
        assert(parsed.volume.maximum == 100);
        assert(parsed.volume.level == 42);
        assert(parsed.volume.muted);
    }

    {
        const std::vector<std::uint8_t> payload(200, 0x55);
        const auto framed = wire::frame(payload);
        std::uint64_t length = 0;
        std::size_t prefix = 0;
        assert(wire::decodeVarintPrefix(framed, length, prefix));
        assert(length == payload.size());
        assert(prefix == 2);
    }

    std::cout << "androidtvremote protocol tests: OK\n";
    return 0;
}
