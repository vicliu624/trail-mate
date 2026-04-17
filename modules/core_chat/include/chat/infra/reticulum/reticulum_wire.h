/**
 * @file reticulum_wire.h
 * @brief Shared Reticulum packet and token helpers for device-side subsets
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::reticulum
{

constexpr size_t kFullHashSize = 32;
constexpr size_t kTruncatedHashSize = 16;
constexpr size_t kNameHashSize = 10;
constexpr size_t kEncryptionPublicKeySize = 32;
constexpr size_t kSigningPublicKeySize = 32;
constexpr size_t kCombinedPublicKeySize = kEncryptionPublicKeySize + kSigningPublicKeySize;
constexpr size_t kSignatureSize = 64;
constexpr size_t kPacketHeader1Size = 2 + kTruncatedHashSize + 1;
constexpr size_t kPacketHeader2Size = 2 + kTruncatedHashSize + kTruncatedHashSize + 1;
constexpr size_t kReticulumMtu = 500;
constexpr size_t kTokenIvSize = 16;
constexpr size_t kTokenHmacSize = 32;
constexpr size_t kTokenOverhead = kTokenIvSize + kTokenHmacSize;
constexpr size_t kDerivedTokenKeySize = 64;
constexpr size_t kReticulumMdu = kReticulumMtu - (2 + 1 + (kTruncatedHashSize * 2));

enum class PacketType : uint8_t
{
    Data = 0x00,
    Announce = 0x01,
    LinkRequest = 0x02,
    Proof = 0x03
};

enum class DestinationType : uint8_t
{
    Single = 0x00,
    Group = 0x01,
    Plain = 0x02,
    Link = 0x03
};

enum class TransportType : uint8_t
{
    Broadcast = 0x00,
    Transport = 0x01,
    Relay = 0x02,
    Tunnel = 0x03
};

enum class PacketContext : uint8_t
{
    None = 0x00,
    Resource = 0x01,
    ResourceAdv = 0x02,
    ResourceReq = 0x03,
    ResourceHmu = 0x04,
    ResourcePrf = 0x05,
    ResourceIcl = 0x06,
    ResourceRcl = 0x07,
    CacheRequest = 0x08,
    Request = 0x09,
    Response = 0x0A,
    PathResponse = 0x0B,
    Command = 0x0C,
    CommandStatus = 0x0D,
    Channel = 0x0E,
    Keepalive = 0xFA,
    LinkIdentify = 0xFB,
    LinkClose = 0xFC,
    LinkProof = 0xFD,
    LrRtt = 0xFE,
    LrProof = 0xFF
};

struct ParsedPacket
{
    bool valid = false;
    uint8_t raw_flags = 0;
    uint8_t header_type = 0;
    uint8_t hops = 0;
    PacketType packet_type = PacketType::Data;
    DestinationType destination_type = DestinationType::Single;
    TransportType transport_type = TransportType::Broadcast;
    uint8_t context = 0;
    uint8_t context_flag = 0;
    const uint8_t* transport_id = nullptr;
    const uint8_t* destination_hash = nullptr;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    size_t header_len = 0;
};

struct ParsedAnnounce
{
    bool valid = false;
    bool has_ratchet = false;
    const uint8_t* public_key = nullptr;
    const uint8_t* name_hash = nullptr;
    const uint8_t* random_hash = nullptr;
    const uint8_t* ratchet = nullptr;
    size_t ratchet_len = 0;
    const uint8_t* signature = nullptr;
    const uint8_t* app_data = nullptr;
    size_t app_data_len = 0;
};

size_t paddedTokenPlaintextSize(size_t plaintext_len);
size_t tokenSizeForPlaintext(size_t plaintext_len);

void fullHash(const uint8_t* data, size_t len, uint8_t out_hash[kFullHashSize]);
void truncatedHash(const uint8_t* data, size_t len, uint8_t out_hash[kTruncatedHashSize]);
void computeNameHash(const char* app_name, const char* aspect,
                     uint8_t out_hash[kNameHashSize]);
void computeIdentityHash(const uint8_t public_key[kCombinedPublicKeySize],
                         uint8_t out_hash[kTruncatedHashSize]);
void computePlainDestinationHash(const uint8_t name_hash[kNameHashSize],
                                 uint8_t out_hash[kTruncatedHashSize]);
void computeDestinationHash(const uint8_t name_hash[kNameHashSize],
                            const uint8_t identity_hash[kTruncatedHashSize],
                            uint8_t out_hash[kTruncatedHashSize]);
void computePacketHash(const uint8_t* raw_packet, size_t len,
                       uint8_t out_hash[kFullHashSize]);
void computeTruncatedPacketHash(const uint8_t* raw_packet, size_t len,
                                uint8_t out_hash[kTruncatedHashSize]);
uint32_t nodeIdFromDestinationHash(const uint8_t destination_hash[kTruncatedHashSize]);

bool parsePacket(const uint8_t* data, size_t len, ParsedPacket* out_packet);
bool buildHeader1Packet(PacketType packet_type,
                        DestinationType destination_type,
                        PacketContext context,
                        bool context_flag,
                        const uint8_t destination_hash[kTruncatedHashSize],
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* out_packet, size_t* inout_len,
                        uint8_t hops = 0,
                        TransportType transport_type = TransportType::Broadcast);
bool buildHeader2Packet(PacketType packet_type,
                        DestinationType destination_type,
                        PacketContext context,
                        bool context_flag,
                        const uint8_t transport_id[kTruncatedHashSize],
                        const uint8_t destination_hash[kTruncatedHashSize],
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* out_packet, size_t* inout_len,
                        uint8_t hops = 0,
                        TransportType transport_type = TransportType::Transport);

bool parseAnnounce(const ParsedPacket& packet, ParsedAnnounce* out_announce);

bool hkdfSha256(const uint8_t* ikm, size_t ikm_len,
                const uint8_t* salt, size_t salt_len,
                const uint8_t* info, size_t info_len,
                uint8_t* out_key, size_t out_len);

bool tokenEncrypt(const uint8_t derived_key[kDerivedTokenKeySize],
                  const uint8_t iv[kTokenIvSize],
                  const uint8_t* plaintext, size_t plaintext_len,
                  uint8_t* out_token, size_t* inout_len);

bool tokenDecrypt(const uint8_t derived_key[kDerivedTokenKeySize],
                  const uint8_t* token, size_t token_len,
                  uint8_t* out_plaintext, size_t* inout_len);

} // namespace chat::reticulum
