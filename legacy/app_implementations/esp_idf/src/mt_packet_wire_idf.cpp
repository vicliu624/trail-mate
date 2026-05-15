#include "chat/infra/meshtastic/mt_packet_wire.h"

#include "mbedtls/aes.h"

#include <cstring>

namespace chat
{
namespace meshtastic
{
namespace
{

void aes_ctr_crypt(const uint8_t* key,
                   size_t key_len,
                   uint8_t* nonce,
                   uint8_t* buffer,
                   size_t len)
{
    if (!key || !nonce || !buffer || len == 0)
    {
        return;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_enc(&aes, key, static_cast<unsigned>(key_len * 8U)) != 0)
    {
        mbedtls_aes_free(&aes);
        return;
    }

    size_t nc_off = 0;
    unsigned char stream_block[16] = {};
    (void)mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream_block, buffer, buffer);
    mbedtls_aes_free(&aes);
}

} // namespace

bool buildWirePacket(const uint8_t* data_payload, size_t data_len,
                     uint32_t from_node, uint32_t packet_id,
                     uint32_t dest_node, uint8_t channel_hash,
                     uint8_t hop_limit, bool want_ack,
                     const uint8_t* psk, size_t psk_len,
                     uint8_t* out_buffer, size_t* out_size)
{
    if (!data_payload || data_len == 0 || !out_buffer || !out_size)
    {
        return false;
    }

    uint8_t payload[256];
    if (data_len > sizeof(payload))
    {
        return false;
    }
    std::memcpy(payload, data_payload, data_len);

    if (psk && psk_len > 0)
    {
        uint8_t nonce[16] = {};
        const uint64_t packet_id64 = static_cast<uint64_t>(packet_id);
        std::memcpy(nonce, &packet_id64, sizeof(packet_id64));
        std::memcpy(nonce + sizeof(packet_id64), &from_node, sizeof(from_node));
        aes_ctr_crypt(psk, psk_len, nonce, payload, data_len);
    }

    PacketHeaderWire header{};
    header.to = dest_node;
    header.from = from_node;
    header.id = packet_id;
    header.flags =
        (hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
        ((hop_limit << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK) |
        (want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0);
    header.channel = channel_hash;
    header.next_hop = 0;
    header.relay_node = static_cast<uint8_t>(from_node & 0xFFU);

    const size_t required = sizeof(header) + data_len;
    if (*out_size < required)
    {
        *out_size = required;
        return false;
    }

    std::memcpy(out_buffer, &header, sizeof(header));
    std::memcpy(out_buffer + sizeof(header), payload, data_len);
    *out_size = required;
    return true;
}

bool parseWirePacket(const uint8_t* buffer, size_t size,
                     PacketHeaderWire* out_header,
                     uint8_t* out_payload, size_t* out_payload_size)
{
    if (!buffer || size < sizeof(PacketHeaderWire) || !out_header || !out_payload || !out_payload_size)
    {
        return false;
    }

    const size_t payload_len = size - sizeof(PacketHeaderWire);
    if (*out_payload_size < payload_len)
    {
        *out_payload_size = payload_len;
        return false;
    }

    std::memcpy(out_header, buffer, sizeof(PacketHeaderWire));
    std::memcpy(out_payload, buffer + sizeof(PacketHeaderWire), payload_len);
    *out_payload_size = payload_len;
    return true;
}

bool decryptPayload(const PacketHeaderWire& header,
                    const uint8_t* cipher, size_t cipher_len,
                    const uint8_t* psk, size_t psk_len,
                    uint8_t* out_plaintext, size_t* out_plain_len)
{
    if (!cipher || cipher_len == 0 || !psk || psk_len == 0 || !out_plaintext || !out_plain_len)
    {
        return false;
    }

    if (*out_plain_len < cipher_len)
    {
        *out_plain_len = cipher_len;
        return false;
    }

    std::memcpy(out_plaintext, cipher, cipher_len);
    uint8_t nonce[16] = {};
    const uint64_t packet_id64 = static_cast<uint64_t>(header.id);
    std::memcpy(nonce, &packet_id64, sizeof(packet_id64));
    std::memcpy(nonce + sizeof(packet_id64), &header.from, sizeof(header.from));
    aes_ctr_crypt(psk, psk_len, nonce, out_plaintext, cipher_len);
    *out_plain_len = cipher_len;
    return true;
}

} // namespace meshtastic
} // namespace chat
