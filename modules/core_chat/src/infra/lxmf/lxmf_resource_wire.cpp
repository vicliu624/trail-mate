#include "chat/infra/lxmf/lxmf_resource_wire.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace chat::lxmf::runtime
{
uint16_t resourcePartCapacity(uint16_t mtu)
{
    constexpr size_t overhead = reticulum::kPacketHeader2Size + 1;
    return mtu > overhead && mtu <= reticulum::kReticulumMtu ? uint16_t(mtu - overhead) : 0;
}
size_t resourceHashmapCapacity(uint16_t mdu)
{
    return mdu > kResourceAdvertisementBudget ? std::max<size_t>(1, (mdu - kResourceAdvertisementBudget) / kResourceMapHashLen) : 1;
}
bool resourceJoinedHash(const uint8_t* first, size_t first_size, const uint8_t* second, size_t second_size, uint8_t hash[32])
{
    return reticulum::fullHashJoined(first, first_size, second, second_size, hash);
}
bool prepareOutgoingResource(const uint8_t* data, size_t size, const uint8_t key[64], uint16_t mdu,
                             uint8_t flags, const uint8_t* request_id, size_t request_size, uint32_t now, uint32_t window,
                             ResourceRandom random, void* context, LinkResourceTransfer& output, ResourceTransform compress)
{
    output = {};
    if (!data || !size || size > kMaxAdvertisedResourceBytes || !key || !mdu || mdu > reticulum::kReticulumMdu || !random || !window ||
        window > kMaxIncomingResourceWindow || (request_size && !request_id)) return false;
    ResourcePayloadBuffer compressed;
    const bool use_compression = compress && compress(data, size, compressed, size) && !compressed.empty() && compressed.size() < size;
    const auto* payload = use_compression ? compressed.data() : data;
    const size_t payload_size = use_compression ? compressed.size() : size;
    ResourcePayloadBuffer stream(kResourcePrefixSize + payload_size);
    if (!random(context, stream.data(), kResourcePrefixSize)) return false;
    std::memcpy(stream.data() + kResourcePrefixSize, payload, payload_size);
    ResourcePayloadBuffer encrypted(reticulum::tokenSizeForPlaintext(stream.size()));
    uint8_t iv[16];
    size_t encrypted_size = encrypted.size();
    if (!random(context, iv, sizeof(iv)) || !reticulum::tokenEncrypt(key, iv, stream.data(), stream.size(), encrypted.data(), &encrypted_size)) return false;
    encrypted.resize(encrypted_size);
    if (encrypted_size > kMaxAdvertisedResourceBytes) return false;
    const size_t parts = (encrypted.size() + mdu - 1) / mdu;
    if (!parts || parts > kMaxIncomingResourceParts ||
        !initialiseOutgoingResourceTransfer(output, request_id, request_size, static_cast<uint32_t>(size),
                                            static_cast<uint32_t>(encrypted.size()), static_cast<uint32_t>(parts), (flags & ~2u) | 1u | (use_compression ? 2u : 0u), now, window)) return false;
    output.compressed = use_compression;
    const size_t guard = window * 2 + resourceHashmapCapacity(mdu);
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        if (!random(context, output.random_hash, sizeof(output.random_hash)))
        {
            output = {};
            return false;
        }
        if (!resourceJoinedHash(data, size, output.random_hash, sizeof(output.random_hash), output.resource_hash))
        {
            output = {};
            return false;
        }
        std::memcpy(output.original_hash, output.resource_hash, 32);
        if (!resourceJoinedHash(data, size, output.resource_hash, 32, output.expected_proof))
        {
            output = {};
            return false;
        }
        output.hashmap.clear();
        RuntimeMapHashList recent;
        bool collision = false;
        for (size_t index = 0; index < parts; ++index)
        {
            const size_t offset = index * mdu, count = std::min<size_t>(mdu, encrypted.size() - offset);
            output.parts[index].assign(encrypted.begin() + offset, encrypted.begin() + offset + count);
            uint8_t hash[32];
            if (!resourceJoinedHash(output.parts[index].data(), count, output.random_hash, 4, hash))
            {
                output = {};
                return false;
            }
            std::array<uint8_t, 4> map{};
            std::memcpy(map.data(), hash, map.size());
            if (std::find(recent.begin(), recent.end(), map) != recent.end())
            {
                collision = true;
                break;
            }
            output.map_hashes[index] = map;
            output.hashmap.insert(output.hashmap.end(), map.begin(), map.end());
            recent.push_back(map);
            if (recent.size() > guard) recent.erase(recent.begin());
        }
        if (!collision) return true;
    }
    output = {};
    return false;
}
bool decodeCompletedResource(const LinkResourceTransfer& resource, const uint8_t key[64], ResourcePayloadBuffer& output, uint8_t proof[32], ResourceTransform expand)
{
    output.clear();
    if (proof) std::memset(proof, 0, 32);
    if (!key || !proof || !resource.encrypted || (resource.compressed && !expand) || resource.has_metadata || resource.split ||
        resource.transfer_size > kMaxAdvertisedResourceBytes + 128 || resource.data_size > kMaxAdvertisedResourceBytes ||
        !resource.part_count || resource.part_count > kMaxIncomingResourceParts || resource.parts.size() != resource.part_count) return false;
    ResourcePayloadBuffer stream;
    stream.reserve(resource.transfer_size);
    for (const auto& part : resource.parts)
    {
        if (part.empty() || part.size() > resource.transfer_size - stream.size()) return false;
        stream.insert(stream.end(), part.begin(), part.end());
    }
    if (stream.size() != resource.transfer_size) return false;
    ResourcePayloadBuffer plain(stream.size());
    size_t plain_size = plain.size();
    if (!reticulum::tokenDecrypt(key, stream.data(), stream.size(), plain.data(), &plain_size) || plain_size < kResourcePrefixSize) return false;
    ResourcePayloadBuffer expanded;
    const uint8_t* payload = plain.data() + kResourcePrefixSize;
    size_t payload_size = plain_size - kResourcePrefixSize;
    if (resource.compressed)
    {
        if (!expand(payload, payload_size, expanded, resource.data_size) || expanded.size() != resource.data_size) return false;
        payload = expanded.data();
        payload_size = expanded.size();
    }
    if (payload_size != resource.data_size) return false;
    uint8_t hash[32];
    if (!resourceJoinedHash(payload, payload_size, resource.random_hash, 4, hash)) return false;
    if (std::memcmp(hash, resource.resource_hash, 32)) return false;
    if (!resourceJoinedHash(payload, payload_size, resource.resource_hash, 32, proof)) return false;
    output.assign(payload, payload + payload_size);
    return true;
}
} // namespace chat::lxmf::runtime
