#include "platform/esp/arduino_common/chat/infra/reticulum/native_gateway_discovery.h"
#include "chat/infra/reticulum/interface_discovery.h"
#include <cstring>

namespace chat::reticulum
{
bool NativeGatewayDiscovery::matches(const ParsedPacket& packet)
{
    if (!packet.valid || packet.packet_type != PacketType::Announce ||
        packet.destination_type != DestinationType::Single || (packet.raw_flags & 0x80)) return false;
    ParsedAnnounce announce{};
    if (!parseAnnounce(packet, &announce) || !announce.name_hash || announce.app_data_len <= 33) return false;
    uint8_t name[kNameHashSize];
    computeNameHash("rnstransport", "discovery.interface", name);
    return std::memcmp(name, announce.name_hash, sizeof(name)) == 0;
}

bool NativeGatewayDiscovery::consume(uint32_t now)
{
    if (verifier_.state() == DiscoveryStampVerifier::State::Pending ||
        (attempted_ && static_cast<uint32_t>(now - last_attempt_) < 10000)) return false;
    attempted_ = true;
    last_attempt_ = now;
    return true;
}

bool NativeGatewayDiscovery::offerVerified(const uint8_t* data, size_t size, const uint8_t identity[16])
{
    if (!identity || verifier_.state() == DiscoveryStampVerifier::State::Pending) return false;
    InterfaceDiscoveryView view{};
    if (!parseInterfaceDiscovery(data, size, view)) return false;
    if (!verifier_.begin(view.packed, view.packed_size, view.stamp)) return false;
    pending_ = {};
    std::memcpy(pending_.host, view.host, view.host_size);
    pending_.port = view.port;
    std::memcpy(pending_.network_identity, identity, 16);
    std::memcpy(pending_.transport_identity, view.transport_id, 16);
    return true;
}

bool NativeGatewayDiscovery::poll()
{
    if (verifier_.state() != DiscoveryStampVerifier::State::Pending) return false;
    const auto state = verifier_.poll();
    if (state == DiscoveryStampVerifier::State::Pending) return false;
    const bool accepted = state == DiscoveryStampVerifier::State::Valid;
    if (accepted) latest_ = pending_;
    pending_ = {};
    verifier_.reset();
    return accepted;
}

void NativeGatewayDiscovery::reset()
{
    verifier_.reset();
    pending_ = {};
    latest_ = {};
    last_attempt_ = 0;
    attempted_ = false;
}
} // namespace chat::reticulum
