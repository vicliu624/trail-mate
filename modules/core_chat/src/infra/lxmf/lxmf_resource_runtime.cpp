/**
 * @file lxmf_resource_runtime.cpp
 * @brief Resource runtime lifecycle helpers for the embedded Reticulum/LXMF adapter
 */

#include "chat/infra/lxmf/lxmf_resource_runtime.h"

#include <algorithm>
#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

uint32_t ageSince(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

bool shouldDropResource(const LinkResourceTransfer& resource,
                        uint32_t now_ms,
                        const ResourceRuntimeLimits& limits)
{
    return resource.complete || resource.last_activity_ms == 0 ||
           ageSince(now_ms, resource.last_activity_ms) > limits.resource_transfer_ttl_ms;
}

bool shouldDropAssembly(const LinkResourceAssembly& assembly,
                        uint32_t now_ms,
                        const ResourceRuntimeLimits& limits)
{
    return assembly.last_activity_ms == 0 ||
           ageSince(now_ms, assembly.last_activity_ms) > limits.resource_transfer_ttl_ms;
}

bool resourceIsComplete(const LinkResourceTransfer& resource)
{
    for (uint8_t received : resource.received_bitmap)
    {
        if (received == 0)
        {
            return false;
        }
    }
    return !resource.received_bitmap.empty();
}

} // namespace

LinkResourceTransfer* findLinkResource(
    LinkResourceTransferList& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    if (!resource_hash)
    {
        return nullptr;
    }

    for (auto& resource : resources)
    {
        if (hashesEqual(resource.resource_hash, resource_hash, sizeof(resource.resource_hash)))
        {
            return &resource;
        }
    }
    return nullptr;
}

const LinkResourceTransfer* findLinkResource(
    const LinkResourceTransferList& resources,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    if (!resource_hash)
    {
        return nullptr;
    }

    for (const auto& resource : resources)
    {
        if (hashesEqual(resource.resource_hash, resource_hash, sizeof(resource.resource_hash)))
        {
            return &resource;
        }
    }
    return nullptr;
}

bool eraseLinkResourceByHash(LinkResourceTransferList& resources,
                             const uint8_t resource_hash[reticulum::kFullHashSize])
{
    if (!resource_hash)
    {
        return false;
    }

    const auto old_size = resources.size();
    resources.erase(
        std::remove_if(resources.begin(),
                       resources.end(),
                       [resource_hash](const LinkResourceTransfer& resource)
                       {
                           return hashesEqual(resource.resource_hash,
                                              resource_hash,
                                              reticulum::kFullHashSize);
                       }),
        resources.end());
    return resources.size() != old_size;
}

LinkResourceAssembly* findLinkResourceAssembly(
    LinkSession& session,
    const uint8_t original_hash[reticulum::kFullHashSize])
{
    if (!original_hash)
    {
        return nullptr;
    }

    for (auto& assembly : session.incoming_resource_assemblies)
    {
        if (hashesEqual(assembly.original_hash, original_hash, sizeof(assembly.original_hash)))
        {
            return &assembly;
        }
    }

    return nullptr;
}

bool eraseLinkResourceAssemblyByOriginalHash(
    LinkSession& session,
    const uint8_t original_hash[reticulum::kFullHashSize])
{
    if (!original_hash)
    {
        return false;
    }

    const auto old_size = session.incoming_resource_assemblies.size();
    session.incoming_resource_assemblies.erase(
        std::remove_if(session.incoming_resource_assemblies.begin(),
                       session.incoming_resource_assemblies.end(),
                       [original_hash](const LinkResourceAssembly& assembly)
                       {
                           return hashesEqual(assembly.original_hash,
                                              original_hash,
                                              sizeof(assembly.original_hash));
                       }),
        session.incoming_resource_assemblies.end());
    return session.incoming_resource_assemblies.size() != old_size;
}

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
    uint32_t window_size)
{
    const uint32_t effective_segments = total_segments == 0 ? 1U : total_segments;
    if (!resource_hash || !random_hash || !original_hash || part_count == 0 ||
        (request_id_len != 0 && !request_id) || !hashmap || hashmap_len == 0 ||
        (hashmap_len % kResourceMapHashLen) != 0 ||
        data_size > kMaxAdvertisedResourceBytes ||
        transfer_size > kMaxAdvertisedResourceBytes ||
        part_count > kMaxIncomingResourceParts ||
        hashmap_len > static_cast<std::size_t>(part_count) * kResourceMapHashLen ||
        effective_segments > kMaxIncomingResourceSegments ||
        segment_index == 0 || segment_index > effective_segments || window_size == 0 ||
        window_size > kMaxIncomingResourceWindow)
    {
        return false;
    }

    resource = LinkResourceTransfer{};
    copyHash(resource.resource_hash, resource_hash, sizeof(resource.resource_hash));
    copyHash(resource.random_hash, random_hash, sizeof(resource.random_hash));
    copyHash(resource.original_hash, original_hash, sizeof(resource.original_hash));
    if (request_id_len != 0)
    {
        resource.request_id.assign(request_id, request_id + request_id_len);
    }
    resource.hashmap.assign(hashmap, hashmap + hashmap_len);
    resource.data_size = data_size;
    resource.transfer_size = transfer_size;
    resource.part_count = part_count;
    resource.segment_index = segment_index;
    resource.total_segments = total_segments;
    resource.hashmap_height = static_cast<uint32_t>(resource.hashmap.size() / kResourceMapHashLen);
    resource.window_size = window_size;
    resource.created_ms = now_ms;
    resource.last_activity_ms = now_ms;
    resource.flags = flags;
    resource.incoming = true;
    resource.encrypted = encrypted;
    resource.compressed = compressed;
    resource.has_metadata = has_metadata;
    resource.split = split || total_segments > 1;
    resource.parts.resize(part_count);
    resource.received_bitmap.assign(part_count, 0);
    resource.map_hashes.resize(part_count);
    resource.map_hash_known.assign(part_count, 0);

    std::size_t map_index = 0;
    for (std::size_t i = 0; i < resource.hashmap.size() && map_index < resource.part_count;
         i += kResourceMapHashLen)
    {
        std::array<uint8_t, kResourceMapHashLen> map_hash{};
        std::memcpy(map_hash.data(), resource.hashmap.data() + i, map_hash.size());
        resource.map_hashes[map_index] = map_hash;
        resource.map_hash_known[map_index] = 1;
        ++map_index;
    }
    resource.waiting_for_hashmap = resource.hashmap_height < resource.part_count;
    return true;
}

bool initialiseOutgoingResourceTransfer(
    LinkResourceTransfer& resource,
    const uint8_t* request_id,
    std::size_t request_id_len,
    uint32_t data_size,
    uint32_t transfer_size,
    uint32_t part_count,
    uint8_t flags,
    uint32_t now_ms,
    uint32_t window_size)
{
    if (part_count == 0 || (request_id_len != 0 && !request_id))
    {
        return false;
    }

    resource = LinkResourceTransfer{};
    if (request_id_len != 0)
    {
        resource.request_id.assign(request_id, request_id + request_id_len);
    }
    resource.data_size = data_size;
    resource.transfer_size = transfer_size;
    resource.part_count = part_count;
    resource.hashmap_height = part_count;
    resource.window_size = window_size;
    resource.created_ms = now_ms;
    resource.last_activity_ms = now_ms;
    resource.flags = flags;
    resource.incoming = false;
    resource.encrypted = true;
    resource.parts.resize(part_count);
    resource.map_hashes.resize(part_count);
    resource.map_hash_known.assign(part_count, 1);
    resource.received_bitmap.assign(part_count, 0);
    return true;
}

ResourceWindowRequest buildNextResourceWindowRequest(
    const LinkResourceTransfer& resource)
{
    ResourceWindowRequest request{};
    if (resource.complete || resource.part_count == 0)
    {
        return request;
    }

    const std::size_t start_index =
        (resource.consecutive_complete_index >= 0)
            ? static_cast<std::size_t>(resource.consecutive_complete_index + 1)
            : 0;

    uint32_t requested = 0;
    for (std::size_t index = start_index;
         index < resource.part_count && requested < resource.window_size;
         ++index)
    {
        if (resource.received_bitmap[index] != 0)
        {
            continue;
        }

        if (index >= resource.map_hash_known.size() || resource.map_hash_known[index] == 0)
        {
            request.needs_more_hashmap = true;
            break;
        }

        request.requested_hashes.push_back(resource.map_hashes[index]);
        ++requested;
    }

    if (requested == 0 && !request.needs_more_hashmap)
    {
        return request;
    }

    if (request.needs_more_hashmap)
    {
        if (resource.hashmap_height == 0 || resource.hashmap_height > resource.map_hashes.size())
        {
            return ResourceWindowRequest{};
        }
        request.last_known_hash = resource.map_hashes[resource.hashmap_height - 1U];
    }

    request.valid = true;
    return request;
}

void noteResourceWindowRequest(LinkResourceTransfer& resource,
                               bool waiting_for_hashmap,
                               uint32_t now_ms)
{
    resource.last_activity_ms = now_ms;
    resource.waiting_for_hashmap = waiting_for_hashmap;
}

bool applyResourceHashmapUpdate(LinkResourceTransfer& resource,
                                uint32_t segment,
                                const uint8_t* hashmap,
                                std::size_t hashmap_len,
                                std::size_t segment_capacity,
                                uint32_t now_ms)
{
    if (segment_capacity == 0 || !hashmap || hashmap_len == 0 ||
        (hashmap_len % kResourceMapHashLen) != 0)
    {
        return false;
    }

    const std::size_t start_index = static_cast<std::size_t>(segment) * segment_capacity;
    if (start_index >= resource.part_count)
    {
        return false;
    }

    const std::size_t hash_count = hashmap_len / kResourceMapHashLen;
    const std::size_t applied =
        std::min(hash_count, static_cast<std::size_t>(resource.part_count) - start_index);
    for (std::size_t index = 0; index < applied; ++index)
    {
        std::array<uint8_t, kResourceMapHashLen> map_hash{};
        std::memcpy(map_hash.data(),
                    hashmap + (index * kResourceMapHashLen),
                    map_hash.size());
        resource.map_hashes[start_index + index] = map_hash;
        resource.map_hash_known[start_index + index] = 1;
    }

    resource.hashmap_height = std::max<uint32_t>(
        resource.hashmap_height,
        static_cast<uint32_t>(start_index + applied));
    resource.last_activity_ms = now_ms;
    resource.waiting_for_hashmap = false;
    return true;
}

bool recordResourcePart(LinkResourceTransfer& resource,
                        const uint8_t* payload,
                        std::size_t payload_len,
                        const uint8_t full_hash[reticulum::kFullHashSize],
                        uint32_t now_ms,
                        std::size_t* out_matched_index,
                        bool* out_complete)
{
    if ((!payload && payload_len != 0) || payload_len == 0 || !full_hash || resource.complete)
    {
        return false;
    }

    std::size_t matched_index = resource.part_count;
    for (std::size_t index = 0;
         index < resource.map_hashes.size() && index < resource.part_count;
         ++index)
    {
        if (index < resource.map_hash_known.size() &&
            resource.map_hash_known[index] != 0 &&
            std::memcmp(resource.map_hashes[index].data(), full_hash, kResourceMapHashLen) == 0)
        {
            matched_index = index;
            break;
        }
    }
    if (matched_index >= resource.part_count)
    {
        return false;
    }

    if (resource.received_bitmap[matched_index] == 0)
    {
        resource.parts[matched_index].assign(payload, payload + payload_len);
        resource.received_bitmap[matched_index] = 1;
    }

    while ((resource.consecutive_complete_index + 1) < static_cast<int32_t>(resource.part_count) &&
           resource.received_bitmap[static_cast<std::size_t>(
               resource.consecutive_complete_index + 1)] != 0)
    {
        ++resource.consecutive_complete_index;
    }
    resource.last_activity_ms = now_ms;

    if (out_matched_index)
    {
        *out_matched_index = matched_index;
    }
    if (out_complete)
    {
        *out_complete = resourceIsComplete(resource);
    }
    return true;
}

ResourceAssemblyResult appendResourceAssemblySegment(
    LinkSession& session,
    LinkResourceTransfer& resource,
    ResourcePayloadBuffer& payload_data,
    uint32_t now_ms)
{
    if (!resource.split && resource.total_segments <= 1)
    {
        return ResourceAssemblyResult::Complete;
    }

    LinkResourceAssembly* assembly =
        findLinkResourceAssembly(session, resource.original_hash);
    if (!assembly)
    {
        if (resource.segment_index != 1)
        {
            return ResourceAssemblyResult::Rejected;
        }

        session.incoming_resource_assemblies.push_back(LinkResourceAssembly{});
        assembly = &session.incoming_resource_assemblies.back();
        copyHash(assembly->original_hash, resource.original_hash, sizeof(assembly->original_hash));
        assembly->next_segment_index = 1;
        assembly->total_segments = resource.total_segments;
        assembly->flags = resource.flags;
        assembly->last_activity_ms = now_ms;
        assembly->request_id = resource.request_id;
    }

    if (assembly->next_segment_index != resource.segment_index)
    {
        return ResourceAssemblyResult::Rejected;
    }

    assembly->payload.insert(assembly->payload.end(),
                             payload_data.begin(),
                             payload_data.end());
    assembly->next_segment_index = resource.segment_index + 1U;
    assembly->last_activity_ms = now_ms;

    if (resource.segment_index < resource.total_segments)
    {
        resource.last_activity_ms = 0;
        return ResourceAssemblyResult::WaitingForNextSegment;
    }

    payload_data = std::move(assembly->payload);
    eraseLinkResourceAssemblyByOriginalHash(session, resource.original_hash);
    return ResourceAssemblyResult::Complete;
}

void markResourceComplete(LinkResourceTransfer& resource, uint32_t now_ms)
{
    resource.complete = true;
    resource.last_activity_ms = now_ms;
}

bool markResourceProofReceived(LinkResourceTransfer& resource,
                               const uint8_t expected_proof[reticulum::kFullHashSize],
                               uint32_t now_ms)
{
    if (!expected_proof ||
        !hashesEqual(expected_proof, resource.expected_proof, reticulum::kFullHashSize))
    {
        return false;
    }

    markResourceComplete(resource, now_ms);
    return true;
}

void cullLinkResources(LinkSession& session,
                       uint32_t now_ms,
                       const ResourceRuntimeLimits& limits)
{
    auto cull_resources = [now_ms, &limits](LinkResourceTransferList& resources)
    {
        resources.erase(
            std::remove_if(resources.begin(),
                           resources.end(),
                           [now_ms, &limits](const LinkResourceTransfer& resource)
                           {
                               return shouldDropResource(resource, now_ms, limits);
                           }),
            resources.end());
    };
    cull_resources(session.incoming_resources);
    cull_resources(session.outgoing_resources);

    session.incoming_resource_assemblies.erase(
        std::remove_if(session.incoming_resource_assemblies.begin(),
                       session.incoming_resource_assemblies.end(),
                       [now_ms, &limits](const LinkResourceAssembly& assembly)
                       {
                           return shouldDropAssembly(assembly, now_ms, limits);
                       }),
        session.incoming_resource_assemblies.end());
}

} // namespace chat::lxmf::runtime
