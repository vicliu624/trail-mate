/**
 * @file lxmf_resource_runtime.h
 * @brief Resource runtime lifecycle helpers for the embedded Reticulum/LXMF adapter
 */

#pragma once

#include "chat/infra/lxmf/lxmf_runtime_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

constexpr std::size_t kResourceMapHashLen = 4;
// RNS 1.4 rejects advertisements above three times MAX_EFFICIENT_SIZE.
// Keep the same wire ceiling and a separate allocation ceiling for ESP.
constexpr uint32_t kMaxAdvertisedResourceBytes = 3U * 1024U * 1024U;
constexpr uint32_t kMaxIncomingResourceParts = 8192U;
constexpr uint32_t kMaxIncomingResourceSegments = 256U;
constexpr uint32_t kMaxIncomingResourceWindow = 64U;

struct ResourceRuntimeLimits
{
    uint32_t resource_transfer_ttl_ms = 0;
};

struct ResourceWindowRequest
{
    bool valid = false;
    bool needs_more_hashmap = false;
    std::array<uint8_t, kResourceMapHashLen> last_known_hash = {};
    RuntimeMapHashList requested_hashes;
};

enum class ResourceAssemblyResult : uint8_t
{
    Complete = 0,
    WaitingForNextSegment = 1,
    Rejected = 2
};

LinkResourceTransfer* findLinkResource(
    LinkResourceTransferList& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize]);
const LinkResourceTransfer* findLinkResource(
    const LinkResourceTransferList& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize]);
bool eraseLinkResourceByHash(LinkResourceTransferList& resources,
                             const uint8_t resource_hash[reticulum::kFullHashSize]);

LinkResourceAssembly* findLinkResourceAssembly(
    LinkSession& session,
    const uint8_t original_hash[reticulum::kFullHashSize]);
bool eraseLinkResourceAssemblyByOriginalHash(
    LinkSession& session,
    const uint8_t original_hash[reticulum::kFullHashSize]);

bool initialiseIncomingResourceTransfer(
    LinkResourceTransfer& resource,
    const uint8_t resource_hash[reticulum::kFullHashSize],
    const uint8_t random_hash[kResourceMapHashLen],
    const uint8_t original_hash[reticulum::kFullHashSize],
    const uint8_t* request_id,
    std::size_t request_id_len,
    const uint8_t* hashmap,
    std::size_t hashmap_len,
    uint32_t data_size,
    uint32_t transfer_size,
    uint32_t part_count,
    uint32_t segment_index,
    uint32_t total_segments,
    uint8_t flags,
    bool encrypted,
    bool compressed,
    bool has_metadata,
    bool split,
    uint32_t now_ms,
    uint32_t window_size);

bool initialiseOutgoingResourceTransfer(
    LinkResourceTransfer& resource,
    const uint8_t* request_id,
    std::size_t request_id_len,
    uint32_t data_size,
    uint32_t transfer_size,
    uint32_t part_count,
    uint8_t flags,
    uint32_t now_ms,
    uint32_t window_size);

ResourceWindowRequest buildNextResourceWindowRequest(
    const LinkResourceTransfer& resource);
void noteResourceWindowRequest(LinkResourceTransfer& resource,
                               bool waiting_for_hashmap,
                               uint32_t now_ms);

bool applyResourceHashmapUpdate(LinkResourceTransfer& resource,
                                uint32_t segment,
                                const uint8_t* hashmap,
                                std::size_t hashmap_len,
                                std::size_t segment_capacity,
                                uint32_t now_ms);

bool recordResourcePart(LinkResourceTransfer& resource,
                        const uint8_t* payload,
                        std::size_t payload_len,
                        const uint8_t full_hash[reticulum::kFullHashSize],
                        uint32_t now_ms,
                        std::size_t* out_matched_index = nullptr,
                        bool* out_complete = nullptr);

ResourceAssemblyResult appendResourceAssemblySegment(
    LinkSession& session,
    LinkResourceTransfer& resource,
    ResourcePayloadBuffer& payload_data,
    uint32_t now_ms);

void markResourceComplete(LinkResourceTransfer& resource, uint32_t now_ms);
bool markResourceProofReceived(LinkResourceTransfer& resource,
                               const uint8_t expected_proof[reticulum::kFullHashSize],
                               uint32_t now_ms);
void cullLinkResources(LinkSession& session,
                       uint32_t now_ms,
                       const ResourceRuntimeLimits& limits);

} // namespace chat::lxmf::runtime
