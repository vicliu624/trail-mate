#pragma once
#include "chat/infra/lxmf/lxmf_resource_runtime.h"
namespace chat::lxmf::runtime
{
constexpr size_t kResourcePrefixSize = 4;
constexpr size_t kResourceAdvertisementBudget = 134;
constexpr uint8_t kResourceRequestFlag = 1U << 3;
constexpr uint8_t kResourceResponseFlag = 1U << 4;
using ResourceRandom = bool (*)(void*, uint8_t*, size_t);
using ResourceTransform = bool (*)(const uint8_t*, size_t, ResourcePayloadBuffer&, size_t limit);
size_t resourceHashmapCapacity(uint16_t mdu);
// Resource parts carry an already encrypted stream, so their capacity differs
// from the MDU of individually encrypted link packets.
uint16_t resourcePartCapacity(uint16_t mtu);
bool resourceJoinedHash(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t hash[32]);
// Input storage must not alias output; output belongs to the session owner.
bool prepareOutgoingResource(const uint8_t* data, size_t size, const uint8_t key[64], uint16_t mdu,
                             uint8_t flags, const uint8_t* request_id, size_t request_size, uint32_t now,
                             uint32_t window, ResourceRandom, void*, LinkResourceTransfer& output, ResourceTransform compress = nullptr);
bool decodeCompletedResource(const LinkResourceTransfer&, const uint8_t key[64],
                             ResourcePayloadBuffer& output, uint8_t proof[32], ResourceTransform expand = nullptr);
} // namespace chat::lxmf::runtime
