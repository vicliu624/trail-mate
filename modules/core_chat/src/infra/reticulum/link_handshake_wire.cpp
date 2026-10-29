#include "chat/infra/reticulum/link_handshake_wire.h"

namespace chat::reticulum
{
void buildLinkSignalling(uint16_t mtu, uint8_t out[kLinkSignallingSize])
{
    if (!out) return;
    const uint32_t value = (static_cast<uint32_t>(mtu) & 0x1fffffU) | 0x200000U;
    out[0] = static_cast<uint8_t>(value >> 16);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value);
}
uint16_t linkPayloadMdu(uint16_t mtu)
{
    if (mtu <= kPacketHeader1Size + kTokenOverhead) return 0;
    const size_t padded = ((mtu - kPacketHeader1Size - kTokenOverhead) / 16U) * 16U;
    return padded ? static_cast<uint16_t>(padded - 1U) : 0;
}
bool computeLinkRequestId(const uint8_t* raw, size_t len, uint8_t out[kTruncatedHashSize])
{
    ParsedPacket packet;
    if (!raw || !out || len > kReticulumMtu || !parsePacket(raw, len, &packet) ||
        packet.packet_type != PacketType::LinkRequest || packet.payload_len < 64) return false;
    computeTruncatedPacketHash(raw, len - (packet.payload_len - 64), out);
    return true;
}
} // namespace chat::reticulum
