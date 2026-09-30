#include "platform/esp/arduino_common/chat/infra/reticulum/native_gateway_discovery.h"
#include <cassert>
#include <cstring>
#include <vector>

using namespace chat::reticulum;

int main()
{
    // Reproduce the stamp with discovery_stamp_reference.py and this packed hex.
    const char* packed = "8500b2544350536572766572496e7465726661636501c302b06e6f64652e6578616d706c652e6f726706cd1092ccfec41042424242424242424242424242424242";
    std::vector<uint8_t> data{0};
    const auto nibble = [](char c)
    { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; packed[i]; i += 2)
        data.push_back(static_cast<uint8_t>((nibble(packed[i]) << 4) | nibble(packed[i + 1])));
    data.insert(data.end(), 32, 0);
    data[data.size() - 2] = 0x1c;
    data.back() = 0xef;
    const auto original = data;
    uint8_t identity[16] = {7};
    NativeGatewayDiscovery discovery;
    assert(discovery.latest().port == 0);
    assert(discovery.consume(UINT32_MAX - 5000));
    assert(!discovery.consume(4998));
    assert(discovery.consume(4999));
    assert(discovery.offerVerified(data.data(), data.size(), identity));
    assert(!discovery.consume(20000));
    assert(!discovery.offerVerified(data.data(), data.size(), identity));
    std::memset(data.data(), 0xff, data.size()); // Receive storage may be reused.
    identity[0] = 9;
    for (unsigned i = 0; i < 19; ++i)
    {
        assert(!discovery.poll());
        assert(discovery.latest().port == 0);
    }
    assert(discovery.poll());
    assert(!discovery.poll());
    assert(std::strcmp(discovery.latest().host, "node.example.org") == 0);
    assert(discovery.latest().port == 4242);
    assert(discovery.latest().network_identity[0] == 7);
    assert(discovery.latest().transport_identity[0] == 0x42);
    data = original;
    data.back() ^= 1;
    assert(discovery.offerVerified(data.data(), data.size(), identity));
    for (unsigned i = 0; i < 20; ++i) assert(!discovery.poll());
    assert(discovery.latest().network_identity[0] == 7); // Invalid cannot replace verified.
    discovery.reset();
    assert(discovery.latest().port == 0 && discovery.consume(0));
    assert(!discovery.offerVerified(nullptr, 0, identity));

    // Matching only selects the service for normal outer signature validation.
    std::vector<uint8_t> payload(64 + 10 + 10 + 64, 0);
    computeNameHash("rnstransport", "discovery.interface", payload.data() + 64);
    payload.insert(payload.end(), original.begin(), original.end());
    ParsedPacket packet{};
    packet.valid = true;
    packet.packet_type = PacketType::Announce;
    packet.destination_type = DestinationType::Single;
    packet.payload = payload.data();
    packet.payload_len = payload.size();
    assert(NativeGatewayDiscovery::matches(packet));
    payload[64] ^= 1;
    assert(!NativeGatewayDiscovery::matches(packet));
    payload[64] ^= 1;
    packet.raw_flags = 0x80;
    assert(!NativeGatewayDiscovery::matches(packet));
    packet.raw_flags = 0;
    packet.packet_type = PacketType::Data;
    assert(!NativeGatewayDiscovery::matches(packet));
    return 0;
}
