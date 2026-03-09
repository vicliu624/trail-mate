/**
 * @file meshcore_payload_helpers.h
 * @brief Shared MeshCore payload/discovery helper utilities
 */

#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat
{
namespace meshcore
{

struct DecodedAdvertAppData
{
    bool valid = false;
    uint8_t node_type = 0;
    bool has_name = false;
    char name[32] = {};
    bool has_location = false;
    int32_t latitude_i6 = 0;
    int32_t longitude_i6 = 0;
};

struct DecodedDiscoverRequest
{
    bool valid = false;
    bool prefix_only = false;
    uint8_t type_filter = 0;
    uint32_t tag = 0;
    uint32_t since = 0;
};

struct DecodedDiscoverResponse
{
    bool valid = false;
    uint8_t node_type = 0;
    int8_t snr_qdb = 0;
    uint32_t tag = 0;
    const uint8_t* pubkey = nullptr;
    size_t pubkey_len = 0;
};

struct DecodedDirectAppPayload
{
    uint32_t portnum = 0;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    bool want_ack = false;
};

struct DecodedGroupAppPayload
{
    NodeId sender = 0;
    uint32_t portnum = 0;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

bool shouldUsePublicChannelFallback(const chat::MeshConfig& cfg);
void xorCrypt(uint8_t* data, size_t len, const uint8_t* key, size_t key_len);
const uint8_t* selectChannelKey(const chat::MeshConfig& cfg, size_t* out_len);
bool isPeerPayloadType(uint8_t payload_type);
bool isPeerCipherShape(size_t payload_len);
bool isAnonReqCipherShape(size_t payload_len);
bool buildFrameNoTransport(uint8_t route_type, uint8_t payload_type,
                           const uint8_t* path, size_t path_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t* out_frame, size_t out_cap, size_t* out_len);
bool buildPeerDatagramPayload(uint8_t dest_hash, uint8_t src_hash,
                              const uint8_t key16[16], const uint8_t key32[32],
                              const uint8_t* plain, size_t plain_len,
                              uint8_t* out_payload, size_t out_cap, size_t* out_len);
bool decodeDirectAppPayload(const uint8_t* plain, size_t plain_len, DecodedDirectAppPayload* out);
bool decodeGroupAppPayload(const uint8_t* plain, size_t plain_len, DecodedGroupAppPayload* out);
bool hasControlPrefix(const uint8_t* payload, size_t len, uint8_t kind);
size_t copySanitizedName(char* out, size_t out_len, const uint8_t* src, size_t src_len);
size_t copyPrintableAscii(const std::string& src, char* out, size_t out_len);
uint8_t mapAdvertTypeToRole(uint8_t node_type);
bool discoverFilterMatchesType(uint8_t filter, uint8_t node_type);
NodeId deriveNodeIdFromPubkey(const uint8_t* pubkey, size_t pubkey_len);
bool decodeAdvertAppData(const uint8_t* app_data, size_t app_data_len, DecodedAdvertAppData* out);
bool decodeDiscoverRequest(const uint8_t* payload, size_t payload_len, DecodedDiscoverRequest* out);
bool decodeDiscoverResponse(const uint8_t* payload, size_t payload_len, DecodedDiscoverResponse* out);
void formatVerificationCode(uint32_t number, char* out_code, size_t out_len);

} // namespace meshcore
} // namespace chat
