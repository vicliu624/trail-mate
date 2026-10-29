/**
 * @file meshcore_payload_helpers.h
 * @brief Shared MeshCore payload/discovery helper utilities
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat
{
namespace meshcore
{

constexpr uint32_t kMeshCoreNodeInfoPortnum = 4;
constexpr uint8_t kMeshCoreControlKindNodeInfo = 0x01;
constexpr uint8_t kMeshCoreNodeInfoFlagRequestReply = 0x01;
constexpr size_t kMeshCoreNodeInfoShortNameFieldSize = 10;
constexpr size_t kMeshCoreNodeInfoLongNameFieldSize = 32;
constexpr size_t kMeshCoreNodeInfoQueryPayloadSize = 5;
constexpr size_t kMeshCoreNodeInfoInfoPayloadSize =
    4 + 1 + 1 + sizeof(NodeId) + sizeof(uint32_t) +
    kMeshCoreNodeInfoShortNameFieldSize + kMeshCoreNodeInfoLongNameFieldSize;
constexpr uint8_t kMeshCoreAdvertTypeChat = 0x01;
constexpr uint8_t kMeshCoreAdvertTypeRepeater = 0x02;
constexpr uint8_t kMeshCoreDiscoverTypeFilterAll = 0xFF;
constexpr size_t kMeshCorePubKeyPrefixSize = 8;
constexpr size_t kMeshCoreDiscoverRequestBasePayloadSize = 6;
constexpr size_t kMeshCoreDiscoverResponseBasePayloadSize = 6;

enum class MeshCoreNodeInfoControlType : uint8_t
{
    Unknown = 0,
    Query = 0x01,
    Info = 0x02,
};

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

struct MeshCoreDiscoverRequestBuildInfo
{
    bool prefix_only = false;
    uint8_t type_filter = kMeshCoreDiscoverTypeFilterAll;
    uint32_t tag = 0;
    uint32_t since = 0;
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

struct MeshCoreNodeInfoBuildInfo
{
    uint8_t role = 0;
    uint8_t hops = 0;
    NodeId node_id = 0;
    uint32_t timestamp = 0;
    const char* short_name = nullptr;
    const char* long_name = nullptr;
};

struct MeshCoreNodeInfoInfo
{
    uint8_t role = 0;
    uint8_t hops = 0;
    NodeId node_id = 0;
    uint32_t timestamp = 0;
    char short_name[kMeshCoreNodeInfoShortNameFieldSize + 1] = {};
    char long_name[kMeshCoreNodeInfoLongNameFieldSize + 1] = {};
};

struct DecodedNodeInfoControl
{
    bool valid = false;
    bool complete = false;
    MeshCoreNodeInfoControlType type = MeshCoreNodeInfoControlType::Unknown;
    bool request_reply = false;
    MeshCoreNodeInfoInfo info{};
};

bool shouldUsePublicChannelFallback(const chat::MeshConfig& cfg);
const uint8_t* publicGroupPsk();
void xorCrypt(uint8_t* data, size_t len, const uint8_t* key, size_t key_len);
const uint8_t* selectChannelKey(const chat::MeshConfig& cfg, size_t* out_len);
bool isPeerPayloadType(uint8_t payload_type);
bool isPeerCipherShape(size_t payload_len);
bool isPeerCipherShape(PayloadProfile profile, size_t payload_len);
bool isAnonReqCipherShape(size_t payload_len);
bool isAnonReqCipherShape(PayloadProfile profile, size_t payload_len);
/// Checks whether a supported MeshCore payload has a protocol-specific wire
/// shape without requiring a locally held key.
bool isPlausibleProtocolPacket(const ParsedPacket& packet);
bool buildFrameNoTransport(uint8_t route_type, uint8_t payload_type,
                           const uint8_t* path, size_t path_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t* out_frame, size_t out_cap, size_t* out_len);
bool buildFrameNoTransport(PayloadProfile profile,
                           uint8_t route_type, uint8_t payload_type,
                           const uint8_t* path, size_t path_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t* out_frame, size_t out_cap, size_t* out_len);
bool buildPeerDatagramPayload(uint8_t dest_hash, uint8_t src_hash,
                              const uint8_t key16[16], const uint8_t key32[32],
                              const uint8_t* plain, size_t plain_len,
                              uint8_t* out_payload, size_t out_cap, size_t* out_len);
bool buildPeerDatagramPayload(PayloadProfile profile,
                              const uint8_t* dest_hash, const uint8_t* src_hash,
                              const uint8_t key16[16], const uint8_t key32[32],
                              const uint8_t* plain, size_t plain_len,
                              uint8_t* out_payload, size_t out_cap, size_t* out_len);
bool decodeDirectAppPayload(const uint8_t* plain, size_t plain_len, DecodedDirectAppPayload* out);
bool decodeGroupAppPayload(const uint8_t* plain, size_t plain_len, DecodedGroupAppPayload* out);
// PATH/ACK has a fixed four-byte token; zero-valued token bytes are not padding.
size_t trimPeerPlaintextPadding(uint8_t payload_type, const uint8_t* plain, size_t padded_size);
// Retains the defined size of fixed NodeInfo controls, including name zeros.
size_t trimGroupPlaintextPadding(const uint8_t* plain, size_t padded_size);
bool isFixedNodeInfoPayload(uint32_t port, const uint8_t* payload, size_t size);
bool hasControlPrefix(const uint8_t* payload, size_t len, uint8_t kind);
bool buildNodeInfoQueryControlPayload(bool request_reply,
                                      uint8_t* out_payload, size_t out_cap, size_t* out_len);
bool buildNodeInfoInfoControlPayload(const MeshCoreNodeInfoBuildInfo& info,
                                     uint8_t* out_payload, size_t out_cap, size_t* out_len);
bool decodeNodeInfoControlPayload(const uint8_t* payload, size_t len, DecodedNodeInfoControl* out);
bool buildDiscoverRequestControlPayload(const MeshCoreDiscoverRequestBuildInfo& request,
                                        uint8_t* out_payload, size_t out_cap, size_t* out_len);
bool buildDiscoverResponseControlPayload(uint8_t node_type, int8_t snr_qdb, uint32_t tag,
                                         const uint8_t* pubkey, size_t pubkey_len,
                                         uint8_t* out_payload, size_t out_cap, size_t* out_len);
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
