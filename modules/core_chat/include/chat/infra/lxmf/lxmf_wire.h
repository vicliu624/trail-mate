/**
 * @file lxmf_wire.h
 * @brief Shared LXMF wire helpers for direct text/app-data subsets
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat::lxmf
{

constexpr uint32_t kFieldTelemetry = 0x02;
constexpr uint32_t kFieldTelemetryStream = 0x03;
constexpr uint32_t kFieldCommands = 0x09;
constexpr uint32_t kFieldCustomType = 0xFB;
constexpr uint32_t kFieldCustomData = 0xFC;

struct DecodedField
{
    uint32_t key = 0;
    std::vector<uint8_t> encoded_value;
};

struct ByteSpan
{
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct ByteSpanList
{
    const ByteSpan* items = nullptr;
    size_t size = 0;
};

enum class CustomDataResult : uint8_t
{
    NotCustom,
    Invalid,
    Valid,
};

using BinItemCallback = bool (*)(const uint8_t* data,
                                 size_t len,
                                 void* context);

struct DecodedEnvelope
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t source_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t signature[reticulum::kSignatureSize] = {};
    std::vector<uint8_t> packed_payload;
};

struct DecodedTextPayload
{
    double timestamp = 0.0;
    std::string title;
    std::string content;
    bool has_stamp = false;
    std::vector<uint8_t> stamp;
    bool fields_empty = true;
    std::vector<DecodedField> fields;
};

struct DecodedMessage : public DecodedEnvelope
{
    double timestamp = 0.0;
    std::string title;
    std::string content;
    bool has_stamp = false;
    std::vector<uint8_t> stamp;
    bool fields_empty = true;
    std::vector<DecodedField> fields;
};

struct SidebandTelemetryLocation
{
    bool valid = false;
    int32_t latitude_e6 = 0;
    int32_t longitude_e6 = 0;
    int32_t altitude_cm = 0;
    uint32_t accuracy_cm = 0;
    uint32_t timestamp = 0;
};

struct SidebandTelemetryRequest
{
    bool valid = false;
    uint32_t timebase = 0;
    bool collector_request = false;
};

struct DecodedAppData
{
    uint8_t version = 0;
    uint32_t portnum = 0;
    uint32_t packet_id = 0;
    uint32_t request_id = 0;
    bool want_response = false;
    std::vector<uint8_t> payload;
};

struct DecodedLinkRequest
{
    double requested_at = 0.0;
    uint8_t path_hash[reticulum::kTruncatedHashSize] = {};
    bool data_is_nil = false;
    std::vector<uint8_t> packed_data;
};

struct DecodedLinkResponse
{
    std::vector<uint8_t> request_id;
    bool data_is_nil = false;
    std::vector<uint8_t> packed_data;
};

struct DecodedResourceAdvertisement
{
    uint32_t transfer_size = 0;
    uint32_t data_size = 0;
    uint32_t part_count = 0;
    uint8_t resource_hash[reticulum::kFullHashSize] = {};
    uint8_t random_hash[4] = {};
    uint8_t original_hash[reticulum::kFullHashSize] = {};
    uint32_t segment_index = 0;
    uint32_t total_segments = 0;
    std::vector<uint8_t> request_id;
    uint8_t flags = 0;
    std::vector<uint8_t> hashmap;
};

struct DecodedResourceHashmapUpdate
{
    uint32_t segment = 0;
    std::vector<uint8_t> hashmap;
};

struct DecodedPropagationBatch
{
    double remote_timebase = 0.0;
    std::vector<std::vector<uint8_t>> messages;
};

struct DecodedPropagationOfferHeader
{
    bool peering_key_is_nil = true;
    ByteSpan peering_key;
};

struct DecodedPropagationOffer
{
    bool peering_key_is_nil = true;
    std::vector<uint8_t> peering_key;
    std::vector<std::vector<uint8_t>> transient_ids;
};

struct DecodedPropagationGetRequestHeader
{
    bool wants_is_nil = true;
    bool haves_is_nil = true;
    bool has_transfer_limit = false;
    uint32_t transfer_limit_kb = 0;
};

struct DecodedPropagationGetRequest
{
    bool wants_is_nil = true;
    std::vector<std::vector<uint8_t>> wants;
    bool haves_is_nil = true;
    std::vector<std::vector<uint8_t>> haves;
    bool has_transfer_limit = false;
    uint32_t transfer_limit_kb = 0;
};

struct DecodedPropagationAnnounce
{
    bool valid = false;
    bool legacy_support = false;
    uint32_t timebase_s = 0;
    bool node_active = false;
    uint32_t transfer_limit_kb = 0;
    uint32_t sync_limit_kb = 0;
    uint8_t stamp_cost = 0;
    uint8_t stamp_cost_flexibility = 0;
    uint8_t peering_cost = 0;
    std::string display_name;
};

bool packPeerAnnounceAppData(const char* display_name,
                             bool has_stamp_cost,
                             uint8_t stamp_cost,
                             uint8_t* out_data,
                             size_t* inout_len);

bool unpackPeerAnnounceAppData(const uint8_t* data, size_t len,
                               char* out_display_name, size_t display_name_len,
                               bool* out_has_stamp_cost,
                               uint8_t* out_stamp_cost);

bool encodeTextPayload(double timestamp,
                       const char* title,
                       const char* content,
                       uint8_t* out_payload,
                       size_t* inout_len);

// Standard custom fields: type is MessagePack str; data is MessagePack bin.
bool encodeCustomDataPayload(double timestamp, const char* title, const char* content,
                             const char* custom_type, ByteSpan custom_data,
                             uint8_t* out_payload, size_t* inout_len);

bool encodeAppDataPayload(uint32_t portnum,
                          uint32_t packet_id,
                          uint32_t request_id,
                          bool want_response,
                          const uint8_t* payload,
                          size_t payload_len,
                          uint8_t* out_payload,
                          size_t* inout_len);

bool encodeLinkRequestPayload(double requested_at,
                              const uint8_t path_hash[reticulum::kTruncatedHashSize],
                              const uint8_t* packed_data,
                              size_t packed_data_len,
                              bool data_is_nil,
                              uint8_t* out_payload,
                              size_t* inout_len);

bool decodeLinkRequestPayload(const uint8_t* data, size_t len, DecodedLinkRequest* out_payload);

bool encodeLinkResponsePayload(const uint8_t* request_id,
                               size_t request_id_len,
                               const uint8_t* packed_data,
                               size_t packed_data_len,
                               bool data_is_nil,
                               uint8_t* out_payload,
                               size_t* inout_len);

bool decodeLinkResponsePayload(const uint8_t* data, size_t len, DecodedLinkResponse* out_payload);

bool encodeResourceAdvertisement(uint32_t transfer_size,
                                 uint32_t data_size,
                                 uint32_t part_count,
                                 const uint8_t resource_hash[reticulum::kFullHashSize],
                                 const uint8_t random_hash[4],
                                 const uint8_t original_hash[reticulum::kFullHashSize],
                                 uint32_t segment_index,
                                 uint32_t total_segments,
                                 const uint8_t* request_id,
                                 size_t request_id_len,
                                 uint8_t flags,
                                 const uint8_t* hashmap,
                                 size_t hashmap_len,
                                 uint8_t* out_payload,
                                 size_t* inout_len);

bool decodeResourceAdvertisement(const uint8_t* data, size_t len,
                                 DecodedResourceAdvertisement* out_advertisement);

bool encodeResourceHashmapUpdate(uint32_t segment,
                                 const uint8_t* hashmap,
                                 size_t hashmap_len,
                                 uint8_t* out_payload,
                                 size_t* inout_len);

bool decodeResourceHashmapUpdate(const uint8_t* data, size_t len,
                                 DecodedResourceHashmapUpdate* out_update);

bool encodeMsgpackBool(bool value,
                       uint8_t* out_payload,
                       size_t* inout_len);

bool encodeMsgpackUint(uint32_t value,
                       uint8_t* out_payload,
                       size_t* inout_len);

bool encodePropagationBatch(double remote_timebase,
                            const std::vector<std::vector<uint8_t>>& messages,
                            uint8_t* out_payload,
                            size_t* inout_len);
bool encodePropagationBatch(double remote_timebase,
                            const std::vector<ByteSpan>& messages,
                            uint8_t* out_payload,
                            size_t* inout_len);
bool encodePropagationBatch(double remote_timebase,
                            ByteSpanList messages,
                            uint8_t* out_payload,
                            size_t* inout_len);

bool decodePropagationBatch(const uint8_t* data, size_t len,
                            DecodedPropagationBatch* out_batch);
bool decodePropagationBatch(const uint8_t* data,
                            size_t len,
                            BinItemCallback on_message,
                            void* callback_context,
                            double* out_remote_timebase);

bool decodePropagationOfferPayload(const uint8_t* data, size_t len,
                                   DecodedPropagationOffer* out_offer);
bool decodePropagationOfferPayload(const uint8_t* data,
                                   size_t len,
                                   BinItemCallback on_transient_id,
                                   void* callback_context,
                                   DecodedPropagationOfferHeader* out_offer);

bool decodePropagationGetRequestPayload(const uint8_t* data, size_t len,
                                        DecodedPropagationGetRequest* out_request);
bool decodePropagationGetRequestPayload(
    const uint8_t* data,
    size_t len,
    BinItemCallback on_want,
    void* want_context,
    BinItemCallback on_have,
    void* have_context,
    DecodedPropagationGetRequestHeader* out_request);

bool encodePropagationGetRequestPayload(
    const std::vector<std::vector<uint8_t>>* wants,
    const std::vector<std::vector<uint8_t>>* haves,
    bool include_transfer_limit,
    uint32_t transfer_limit_kb,
    uint8_t* out_payload,
    size_t* inout_len);
bool encodePropagationGetRequestPayloadSpans(const ByteSpanList* wants,
                                             const ByteSpanList* haves,
                                             bool include_transfer_limit,
                                             uint32_t transfer_limit_kb,
                                             uint8_t* out_payload,
                                             size_t* inout_len);

bool decodePropagationIdListPayload(const uint8_t* data,
                                    size_t len,
                                    std::vector<std::vector<uint8_t>>* out_ids);
bool decodePropagationIdListPayload(const uint8_t* data,
                                    size_t len,
                                    BinItemCallback on_item,
                                    void* callback_context);

bool decodePropagationMessageListPayload(
    const uint8_t* data,
    size_t len,
    std::vector<std::vector<uint8_t>>* out_messages);
bool decodePropagationMessageListPayload(const uint8_t* data,
                                         size_t len,
                                         BinItemCallback on_message,
                                         void* callback_context);

bool decodePropagationAnnounceAppData(
    const uint8_t* data,
    size_t len,
    DecodedPropagationAnnounce* out_announce);

bool encodePropagationIdListPayload(const std::vector<std::vector<uint8_t>>& ids,
                                    uint8_t* out_payload,
                                    size_t* inout_len);
bool encodePropagationIdListPayload(ByteSpanList ids,
                                    uint8_t* out_payload,
                                    size_t* inout_len);

bool encodePropagationMessageListPayload(const std::vector<std::vector<uint8_t>>& messages,
                                         uint8_t* out_payload,
                                         size_t* inout_len);
bool encodePropagationMessageListPayload(const std::vector<ByteSpan>& messages,
                                         uint8_t* out_payload,
                                         size_t* inout_len);
bool encodePropagationMessageListPayload(ByteSpanList messages,
                                         uint8_t* out_payload,
                                         size_t* inout_len);

void computeMessageHash(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                        const uint8_t source_hash[reticulum::kTruncatedHashSize],
                        const uint8_t* packed_payload,
                        size_t packed_payload_len,
                        uint8_t out_hash[reticulum::kFullHashSize]);

bool buildSignedPart(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                     const uint8_t source_hash[reticulum::kTruncatedHashSize],
                     const uint8_t* packed_payload,
                     size_t packed_payload_len,
                     uint8_t* out_signed_part,
                     size_t* inout_len,
                     uint8_t out_message_hash[reticulum::kFullHashSize]);

bool packMessage(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                 const uint8_t source_hash[reticulum::kTruncatedHashSize],
                 const uint8_t signature[reticulum::kSignatureSize],
                 const uint8_t* packed_payload,
                 size_t packed_payload_len,
                 uint8_t* out_message,
                 size_t* inout_len);

bool unpackMessageEnvelope(const uint8_t* data, size_t len, DecodedEnvelope* out_envelope);
bool unpackTextPayload(const uint8_t* data, size_t len, DecodedTextPayload* out_payload);
bool encodeSidebandTelemetryLocationPayload(
    double message_timestamp,
    const SidebandTelemetryLocation& location,
    uint8_t* out_payload,
    size_t* inout_len);
const DecodedField* findField(const DecodedTextPayload& payload, uint32_t key);
// Output borrows payload.fields storage; reject duplicate and wrong-type fields.
CustomDataResult extractCustomData(const DecodedTextPayload& payload,
                                   ByteSpan* out_type, ByteSpan* out_data);
bool decodeSidebandTelemetryLocation(const DecodedTextPayload& payload,
                                     SidebandTelemetryLocation* out_location);
bool decodeSidebandTelemetryRequest(const DecodedTextPayload& payload,
                                    SidebandTelemetryRequest* out_request);
bool decodeAppDataPayload(const uint8_t* data, size_t len, DecodedAppData* out_payload);
bool unpackMessage(const uint8_t* data, size_t len, DecodedMessage* out_message);

} // namespace chat::lxmf
