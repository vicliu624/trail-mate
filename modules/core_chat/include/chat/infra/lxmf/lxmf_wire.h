/**
 * @file lxmf_wire.h
 * @brief Shared LXMF wire helpers for direct text-message subsets
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat::lxmf
{

struct DecodedMessage
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t source_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t signature[reticulum::kSignatureSize] = {};
    double timestamp = 0.0;
    std::string title;
    std::string content;
    std::vector<uint8_t> packed_payload;
    bool has_stamp = false;
    std::vector<uint8_t> stamp;
    bool fields_empty = true;
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

bool unpackMessage(const uint8_t* data, size_t len, DecodedMessage* out_message);

} // namespace chat::lxmf
