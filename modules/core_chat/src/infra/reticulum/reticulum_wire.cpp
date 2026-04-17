/**
 * @file reticulum_wire.cpp
 * @brief Shared Reticulum packet and token helpers for device-side subsets
 */

#include "chat/infra/reticulum/reticulum_wire.h"

#include <AES.h>
#include <Crypto.h>
#include <SHA256.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace chat::reticulum
{
namespace
{
constexpr size_t kAesBlockSize = 16;
constexpr size_t kHeader1Size = 2 + kTruncatedHashSize + 1;
constexpr size_t kHeader2Size = 2 + kTruncatedHashSize + kTruncatedHashSize + 1;
constexpr uint8_t kHeaderType1 = 0x00;
constexpr uint8_t kHeaderType2 = 0x01;

class Aes256CbcCipher
{
  public:
    void setKey(const uint8_t* key, size_t len)
    {
        valid_ = (key != nullptr && len == 32);
        if (valid_)
        {
            aes_.setKey(key, len);
        }
    }

    bool valid() const
    {
        return valid_;
    }

    void encryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return;
        }
        aes_.encryptBlock(out, in);
    }

    void decryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return;
        }
        aes_.decryptBlock(out, in);
    }

  private:
    AESSmall256 aes_;
    bool valid_ = false;
};

void hmacSha256(const uint8_t* key, size_t key_len,
                const uint8_t* data, size_t data_len,
                uint8_t out_hash[kFullHashSize])
{
    if (!out_hash)
    {
        return;
    }

    SHA256 sha;
    sha.resetHMAC(key, key_len);
    if (data && data_len != 0)
    {
        sha.update(data, data_len);
    }
    sha.finalizeHMAC(key, key_len, out_hash, kFullHashSize);
}

void xorBlock(uint8_t* dst, const uint8_t* src)
{
    if (!dst || !src)
    {
        return;
    }
    for (size_t i = 0; i < kAesBlockSize; ++i)
    {
        dst[i] ^= src[i];
    }
}

bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i)
    {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

size_t pkcs7Pad(const uint8_t* input, size_t input_len,
                uint8_t* out, size_t out_len)
{
    const size_t pad_len = kAesBlockSize - (input_len % kAesBlockSize);
    const size_t total_len = input_len + ((pad_len == 0) ? kAesBlockSize : pad_len);
    if (!out || out_len < total_len)
    {
        return 0;
    }

    if (input && input_len != 0)
    {
        memcpy(out, input, input_len);
    }
    const uint8_t applied = static_cast<uint8_t>((pad_len == 0) ? kAesBlockSize : pad_len);
    for (size_t i = input_len; i < total_len; ++i)
    {
        out[i] = applied;
    }
    return total_len;
}

bool pkcs7Unpad(const uint8_t* input, size_t input_len,
                uint8_t* out, size_t* inout_len)
{
    if (!input || input_len == 0 || !out || !inout_len)
    {
        return false;
    }

    const uint8_t pad_len = input[input_len - 1];
    if (pad_len == 0 || pad_len > kAesBlockSize || pad_len > input_len)
    {
        return false;
    }

    for (size_t i = 0; i < pad_len; ++i)
    {
        if (input[input_len - 1 - i] != pad_len)
        {
            return false;
        }
    }

    const size_t plain_len = input_len - pad_len;
    if (*inout_len < plain_len)
    {
        *inout_len = plain_len;
        return false;
    }

    if (plain_len != 0)
    {
        memcpy(out, input, plain_len);
    }
    *inout_len = plain_len;
    return true;
}

void aesCbcEncrypt(const uint8_t* key, size_t key_len,
                   const uint8_t iv[kTokenIvSize],
                   const uint8_t* plaintext, size_t plaintext_len,
                   uint8_t* out_ciphertext)
{
    Aes256CbcCipher cipher;
    cipher.setKey(key, key_len);
    if (!cipher.valid() || !iv || !out_ciphertext)
    {
        return;
    }

    uint8_t previous[kAesBlockSize] = {};
    memcpy(previous, iv, sizeof(previous));

    for (size_t offset = 0; offset < plaintext_len; offset += kAesBlockSize)
    {
        uint8_t block[kAesBlockSize] = {};
        memcpy(block, plaintext + offset, kAesBlockSize);
        xorBlock(block, previous);
        cipher.encryptBlock(out_ciphertext + offset, block);
        memcpy(previous, out_ciphertext + offset, kAesBlockSize);
    }
}

void aesCbcDecrypt(const uint8_t* key, size_t key_len,
                   const uint8_t iv[kTokenIvSize],
                   const uint8_t* ciphertext, size_t ciphertext_len,
                   uint8_t* out_plaintext)
{
    Aes256CbcCipher cipher;
    cipher.setKey(key, key_len);
    if (!cipher.valid() || !iv || !out_plaintext)
    {
        return;
    }

    uint8_t previous[kAesBlockSize] = {};
    memcpy(previous, iv, sizeof(previous));

    for (size_t offset = 0; offset < ciphertext_len; offset += kAesBlockSize)
    {
        uint8_t block[kAesBlockSize] = {};
        cipher.decryptBlock(block, ciphertext + offset);
        xorBlock(block, previous);
        memcpy(out_plaintext + offset, block, kAesBlockSize);
        memcpy(previous, ciphertext + offset, kAesBlockSize);
    }
}

void appendAscii(char* out, size_t out_len, size_t& index, const char* text)
{
    if (!out || out_len == 0 || !text)
    {
        return;
    }
    while (*text != '\0' && index + 1 < out_len)
    {
        out[index++] = *text++;
    }
    out[index] = '\0';
}

} // namespace

size_t paddedTokenPlaintextSize(size_t plaintext_len)
{
    const size_t remainder = plaintext_len % kAesBlockSize;
    return plaintext_len + ((remainder == 0) ? kAesBlockSize : (kAesBlockSize - remainder));
}

size_t tokenSizeForPlaintext(size_t plaintext_len)
{
    return kTokenOverhead + paddedTokenPlaintextSize(plaintext_len);
}

void fullHash(const uint8_t* data, size_t len, uint8_t out_hash[kFullHashSize])
{
    if (!out_hash)
    {
        return;
    }

    SHA256 sha;
    if (data && len != 0)
    {
        sha.update(data, len);
    }
    sha.finalize(out_hash, kFullHashSize);
}

void truncatedHash(const uint8_t* data, size_t len, uint8_t out_hash[kTruncatedHashSize])
{
    uint8_t hash[kFullHashSize] = {};
    fullHash(data, len, hash);
    memcpy(out_hash, hash, kTruncatedHashSize);
}

void computeNameHash(const char* app_name, const char* aspect,
                     uint8_t out_hash[kNameHashSize])
{
    char expanded[64] = {};
    size_t index = 0;
    appendAscii(expanded, sizeof(expanded), index, app_name ? app_name : "");
    if (aspect && aspect[0] != '\0' && index + 1 < sizeof(expanded))
    {
        expanded[index++] = '.';
        expanded[index] = '\0';
        appendAscii(expanded, sizeof(expanded), index, aspect);
    }

    uint8_t full[kFullHashSize] = {};
    fullHash(reinterpret_cast<const uint8_t*>(expanded), strlen(expanded), full);
    memcpy(out_hash, full, kNameHashSize);
}

void computeIdentityHash(const uint8_t public_key[kCombinedPublicKeySize],
                         uint8_t out_hash[kTruncatedHashSize])
{
    truncatedHash(public_key, kCombinedPublicKeySize, out_hash);
}

void computePlainDestinationHash(const uint8_t name_hash[kNameHashSize],
                                 uint8_t out_hash[kTruncatedHashSize])
{
    truncatedHash(name_hash, kNameHashSize, out_hash);
}

void computeDestinationHash(const uint8_t name_hash[kNameHashSize],
                            const uint8_t identity_hash[kTruncatedHashSize],
                            uint8_t out_hash[kTruncatedHashSize])
{
    uint8_t material[kNameHashSize + kTruncatedHashSize] = {};
    memcpy(material, name_hash, kNameHashSize);
    memcpy(material + kNameHashSize, identity_hash, kTruncatedHashSize);
    truncatedHash(material, sizeof(material), out_hash);
}

void computePacketHash(const uint8_t* raw_packet, size_t len,
                       uint8_t out_hash[kFullHashSize])
{
    if (!raw_packet || len < kHeader1Size || !out_hash)
    {
        if (out_hash)
        {
            memset(out_hash, 0, kFullHashSize);
        }
        return;
    }

    uint8_t hashable[kReticulumMtu] = {};
    size_t hashable_len = 0;
    hashable[hashable_len++] = static_cast<uint8_t>(raw_packet[0] & 0x0FU);

    const uint8_t header_type = static_cast<uint8_t>((raw_packet[0] >> 6) & 0x01U);
    if (header_type == kHeaderType2)
    {
        if (len < kHeader2Size)
        {
            memset(out_hash, 0, kFullHashSize);
            return;
        }

        memcpy(hashable + hashable_len,
               raw_packet + 2 + kTruncatedHashSize,
               len - (2 + kTruncatedHashSize));
        hashable_len += (len - (2 + kTruncatedHashSize));
    }
    else
    {
        memcpy(hashable + hashable_len, raw_packet + 2, len - 2);
        hashable_len += (len - 2);
    }

    fullHash(hashable, hashable_len, out_hash);
}

void computeTruncatedPacketHash(const uint8_t* raw_packet, size_t len,
                                uint8_t out_hash[kTruncatedHashSize])
{
    uint8_t full[kFullHashSize] = {};
    computePacketHash(raw_packet, len, full);
    memcpy(out_hash, full, kTruncatedHashSize);
}

uint32_t nodeIdFromDestinationHash(const uint8_t destination_hash[kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<uint32_t>(destination_hash[12]) << 24) |
           (static_cast<uint32_t>(destination_hash[13]) << 16) |
           (static_cast<uint32_t>(destination_hash[14]) << 8) |
           static_cast<uint32_t>(destination_hash[15]);
}

bool parsePacket(const uint8_t* data, size_t len, ParsedPacket* out_packet)
{
    if (!data || len < kHeader1Size || !out_packet)
    {
        return false;
    }

    ParsedPacket parsed{};
    parsed.raw_flags = data[0];
    parsed.hops = data[1];

    parsed.header_type = static_cast<uint8_t>((parsed.raw_flags >> 6) & 0x01U);
    if (parsed.header_type != kHeaderType1 && parsed.header_type != kHeaderType2)
    {
        return false;
    }

    parsed.context_flag = static_cast<uint8_t>((parsed.raw_flags >> 5) & 0x01U);
    parsed.transport_type = static_cast<TransportType>((parsed.raw_flags >> 4) & 0x01U);
    parsed.destination_type = static_cast<DestinationType>((parsed.raw_flags >> 2) & 0x03U);
    parsed.packet_type = static_cast<PacketType>(parsed.raw_flags & 0x03U);

    if (parsed.header_type == kHeaderType2)
    {
        if (len < kHeader2Size)
        {
            return false;
        }

        parsed.transport_id = data + 2;
        parsed.destination_hash = data + 2 + kTruncatedHashSize;
        parsed.context = data[2 + (kTruncatedHashSize * 2)];
        parsed.payload = data + kHeader2Size;
        parsed.payload_len = len - kHeader2Size;
        parsed.header_len = kHeader2Size;
    }
    else
    {
        parsed.transport_id = nullptr;
        parsed.destination_hash = data + 2;
        parsed.context = data[2 + kTruncatedHashSize];
        parsed.payload = data + kHeader1Size;
        parsed.payload_len = len - kHeader1Size;
        parsed.header_len = kHeader1Size;
    }

    parsed.valid = true;

    *out_packet = parsed;
    return true;
}

bool buildHeader1Packet(PacketType packet_type,
                        DestinationType destination_type,
                        PacketContext context,
                        bool context_flag,
                        const uint8_t destination_hash[kTruncatedHashSize],
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* out_packet, size_t* inout_len,
                        uint8_t hops,
                        TransportType transport_type)
{
    if (!destination_hash || !out_packet || !inout_len)
    {
        return false;
    }

    const size_t total_len = kHeader1Size + payload_len;
    if (*inout_len < total_len || total_len > kReticulumMtu)
    {
        *inout_len = total_len;
        return false;
    }

    const uint8_t flags =
        static_cast<uint8_t>((0U << 6) |
                             ((context_flag ? 1U : 0U) << 5) |
                             ((static_cast<uint8_t>(transport_type) & 0x01U) << 4) |
                             ((static_cast<uint8_t>(destination_type) & 0x03U) << 2) |
                             (static_cast<uint8_t>(packet_type) & 0x03U));

    out_packet[0] = flags;
    out_packet[1] = hops;
    memcpy(out_packet + 2, destination_hash, kTruncatedHashSize);
    out_packet[2 + kTruncatedHashSize] = static_cast<uint8_t>(context);
    if (payload && payload_len != 0)
    {
        memcpy(out_packet + kHeader1Size, payload, payload_len);
    }
    *inout_len = total_len;
    return true;
}

bool buildHeader2Packet(PacketType packet_type,
                        DestinationType destination_type,
                        PacketContext context,
                        bool context_flag,
                        const uint8_t transport_id[kTruncatedHashSize],
                        const uint8_t destination_hash[kTruncatedHashSize],
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* out_packet, size_t* inout_len,
                        uint8_t hops,
                        TransportType transport_type)
{
    if (!transport_id || !destination_hash || !out_packet || !inout_len)
    {
        return false;
    }

    const size_t total_len = kHeader2Size + payload_len;
    if (*inout_len < total_len || total_len > kReticulumMtu)
    {
        *inout_len = total_len;
        return false;
    }

    const uint8_t flags =
        static_cast<uint8_t>((1U << 6) |
                             ((context_flag ? 1U : 0U) << 5) |
                             ((static_cast<uint8_t>(transport_type) & 0x01U) << 4) |
                             ((static_cast<uint8_t>(destination_type) & 0x03U) << 2) |
                             (static_cast<uint8_t>(packet_type) & 0x03U));

    out_packet[0] = flags;
    out_packet[1] = hops;
    memcpy(out_packet + 2, transport_id, kTruncatedHashSize);
    memcpy(out_packet + 2 + kTruncatedHashSize, destination_hash, kTruncatedHashSize);
    out_packet[2 + (kTruncatedHashSize * 2)] = static_cast<uint8_t>(context);
    if (payload && payload_len != 0)
    {
        memcpy(out_packet + kHeader2Size, payload, payload_len);
    }
    *inout_len = total_len;
    return true;
}

bool parseAnnounce(const ParsedPacket& packet, ParsedAnnounce* out_announce)
{
    if (!packet.valid || !out_announce ||
        packet.packet_type != PacketType::Announce ||
        packet.payload == nullptr ||
        packet.payload_len < (kCombinedPublicKeySize + kNameHashSize + 10 + kSignatureSize))
    {
        return false;
    }

    ParsedAnnounce parsed{};
    parsed.valid = true;
    parsed.has_ratchet = (packet.context_flag != 0);
    parsed.public_key = packet.payload;
    parsed.name_hash = packet.payload + kCombinedPublicKeySize;
    parsed.random_hash = parsed.name_hash + kNameHashSize;

    if (parsed.has_ratchet)
    {
        return false;
    }

    parsed.signature = parsed.random_hash + 10;
    parsed.app_data = parsed.signature + kSignatureSize;
    parsed.app_data_len = packet.payload_len - (kCombinedPublicKeySize + kNameHashSize + 10 + kSignatureSize);

    *out_announce = parsed;
    return true;
}

bool hkdfSha256(const uint8_t* ikm, size_t ikm_len,
                const uint8_t* salt, size_t salt_len,
                const uint8_t* info, size_t info_len,
                uint8_t* out_key, size_t out_len)
{
    if (!ikm || ikm_len == 0 || !out_key || out_len == 0)
    {
        return false;
    }

    uint8_t zero_salt[kFullHashSize] = {};
    const uint8_t* actual_salt = (salt && salt_len != 0) ? salt : zero_salt;
    const size_t actual_salt_len = (salt && salt_len != 0) ? salt_len : sizeof(zero_salt);

    uint8_t prk[kFullHashSize] = {};
    hmacSha256(actual_salt, actual_salt_len, ikm, ikm_len, prk);

    uint8_t previous[kFullHashSize] = {};
    size_t generated = 0;
    uint8_t counter = 1;
    size_t previous_len = 0;

    while (generated < out_len)
    {
        uint8_t block_input[kFullHashSize + 64 + 1] = {};
        size_t block_len = 0;
        if (previous_len != 0)
        {
            memcpy(block_input + block_len, previous, previous_len);
            block_len += previous_len;
        }
        if (info && info_len != 0)
        {
            memcpy(block_input + block_len, info, info_len);
            block_len += info_len;
        }
        block_input[block_len++] = counter++;

        hmacSha256(prk, sizeof(prk), block_input, block_len, previous);
        previous_len = sizeof(previous);

        const size_t remaining = out_len - generated;
        const size_t chunk = std::min(remaining, sizeof(previous));
        memcpy(out_key + generated, previous, chunk);
        generated += chunk;
    }

    return true;
}

bool tokenEncrypt(const uint8_t derived_key[kDerivedTokenKeySize],
                  const uint8_t iv[kTokenIvSize],
                  const uint8_t* plaintext, size_t plaintext_len,
                  uint8_t* out_token, size_t* inout_len)
{
    if (!derived_key || !iv || !out_token || !inout_len)
    {
        return false;
    }

    const size_t padded_len = paddedTokenPlaintextSize(plaintext_len);
    const size_t total_len = tokenSizeForPlaintext(plaintext_len);
    if (*inout_len < total_len)
    {
        *inout_len = total_len;
        return false;
    }

    std::vector<uint8_t> padded(padded_len, 0);
    if (padded.empty())
    {
        padded.resize(kAesBlockSize, 0);
    }
    if (pkcs7Pad(plaintext, plaintext_len, padded.data(), padded.size()) != padded_len)
    {
        return false;
    }

    memcpy(out_token, iv, kTokenIvSize);
    aesCbcEncrypt(derived_key + 32, 32, iv, padded.data(), padded_len, out_token + kTokenIvSize);

    uint8_t mac[kFullHashSize] = {};
    hmacSha256(derived_key, 32, out_token, kTokenIvSize + padded_len, mac);
    memcpy(out_token + kTokenIvSize + padded_len, mac, sizeof(mac));
    *inout_len = total_len;
    return true;
}

bool tokenDecrypt(const uint8_t derived_key[kDerivedTokenKeySize],
                  const uint8_t* token, size_t token_len,
                  uint8_t* out_plaintext, size_t* inout_len)
{
    if (!derived_key || !token || token_len <= kTokenOverhead || !out_plaintext || !inout_len)
    {
        return false;
    }

    const size_t cipher_len = token_len - kTokenOverhead;
    if ((cipher_len % kAesBlockSize) != 0)
    {
        return false;
    }

    const uint8_t* iv = token;
    const uint8_t* ciphertext = token + kTokenIvSize;
    const uint8_t* received_hmac = token + kTokenIvSize + cipher_len;

    uint8_t expected_hmac[kFullHashSize] = {};
    hmacSha256(derived_key, 32, token, kTokenIvSize + cipher_len, expected_hmac);
    if (!constantTimeEquals(received_hmac, expected_hmac, sizeof(expected_hmac)))
    {
        return false;
    }

    std::vector<uint8_t> padded(cipher_len, 0);
    if (padded.empty())
    {
        return false;
    }

    aesCbcDecrypt(derived_key + 32, 32, iv, ciphertext, cipher_len, padded.data());
    return pkcs7Unpad(padded.data(), cipher_len, out_plaintext, inout_len);
}

} // namespace chat::reticulum
