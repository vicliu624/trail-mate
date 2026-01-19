/**
 * @file mt_packet_wire.cpp
 * @brief Meshtastic wire packet format implementation
 */

#include "mt_packet_wire.h"
#include <AES.h>
#include <Arduino.h>
#include <CTR.h>
#include <cstring>

namespace chat
{
namespace meshtastic
{

namespace
{
constexpr size_t kMaxBlockSize = 256;

void aesCtrCrypt(const uint8_t* key, size_t key_len, uint8_t* nonce,
                 uint8_t* buffer, size_t len)
{
    if (!key || key_len == 0 || !buffer || len == 0)
    {
        return;
    }

    uint8_t scratch[kMaxBlockSize];
    if (len > sizeof(scratch))
    {
        return;
    }
    memcpy(scratch, buffer, len);
    memset(scratch + len, 0, sizeof(scratch) - len);

    if (key_len == 16)
    {
        CTR<AES128> ctr;
        ctr.setKey(key, key_len);
        ctr.setIV(nonce, 16);
        ctr.setCounterSize(4);
        ctr.encrypt(buffer, scratch, len);
    }
    else
    {
        CTR<AES256> ctr;
        ctr.setKey(key, key_len);
        ctr.setIV(nonce, 16);
        ctr.setCounterSize(4);
        ctr.encrypt(buffer, scratch, len);
    }
}
} // namespace

bool buildWirePacket(const uint8_t* data_payload, size_t data_len,
                     uint32_t from_node, uint32_t packet_id,
                     uint32_t dest_node, uint8_t channel_hash,
                     uint8_t hop_limit, bool want_ack,
                     const uint8_t* psk, size_t psk_len,
                     uint8_t* out_buffer, size_t* out_size)
{
    if (!data_payload || !out_buffer || !out_size || data_len == 0)
    {
        return false;
    }

    // Encrypt payload if PSK provided
    uint8_t payload[256];
    size_t payload_len = data_len;
    memcpy(payload, data_payload, data_len);

    if (psk && psk_len > 0)
    {
        uint8_t nonce[16];
        memset(nonce, 0, sizeof(nonce));
        uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
        memcpy(nonce, &packet_id64, sizeof(uint64_t));
        memcpy(nonce + sizeof(uint64_t), &from_node, sizeof(uint32_t));
        aesCtrCrypt(psk, psk_len, nonce, payload, payload_len);
    }

    // Build header
    PacketHeaderWire hdr{};
    hdr.to = dest_node;
    hdr.from = from_node;
    hdr.id = packet_id;

    uint8_t hop_start = hop_limit;
    uint8_t flags = (hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
                    ((hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK);
    if (want_ack)
    {
        flags |= PACKET_FLAGS_WANT_ACK_MASK;
    }
    hdr.flags = flags;
    hdr.channel = channel_hash;
    hdr.next_hop = 0;
    hdr.relay_node = static_cast<uint8_t>(from_node & 0xFF);

    // Build full packet
    size_t required_size = sizeof(hdr) + payload_len;
    if (*out_size < required_size)
    {
        *out_size = required_size;
        return false;
    }

    memcpy(out_buffer, &hdr, sizeof(hdr));
    memcpy(out_buffer + sizeof(hdr), payload, payload_len);

    *out_size = required_size;
    return true;
}

bool parseWirePacket(const uint8_t* buffer, size_t size,
                     PacketHeaderWire* out_header,
                     uint8_t* out_payload, size_t* out_payload_size)
{
    if (!buffer || !out_header || !out_payload || !out_payload_size || size < sizeof(PacketHeaderWire))
    {
        return false;
    }

    // Extract header
    memcpy(out_header, buffer, sizeof(PacketHeaderWire));

    // Extract payload
    size_t payload_len = size - sizeof(PacketHeaderWire);
    if (payload_len > *out_payload_size)
    {
        *out_payload_size = payload_len;
        return false;
    }

    memcpy(out_payload, buffer + sizeof(PacketHeaderWire), payload_len);
    *out_payload_size = payload_len;
    return true;
}

bool decryptPayload(const PacketHeaderWire& header,
                    const uint8_t* cipher, size_t cipher_len,
                    const uint8_t* psk, size_t psk_len,
                    uint8_t* out_plaintext, size_t* out_plain_len)
{
    if (!cipher || !psk || !out_plaintext || cipher_len == 0)
    {
        return false;
    }

    if (*out_plain_len < cipher_len)
    {
        *out_plain_len = cipher_len;
        return false;
    }

    // Build nonce
    uint8_t nonce[16];
    memset(nonce, 0, 16);
    uint64_t packet_id64 = static_cast<uint64_t>(header.id);
    memcpy(nonce, &packet_id64, sizeof(uint64_t));
    memcpy(nonce + sizeof(uint64_t), &header.from, sizeof(uint32_t));

    memcpy(out_plaintext, cipher, cipher_len);
    aesCtrCrypt(psk, psk_len, nonce, out_plaintext, cipher_len);

    *out_plain_len = cipher_len;
    return true;
}

} // namespace meshtastic
} // namespace chat
