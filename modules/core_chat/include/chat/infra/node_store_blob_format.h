/**
 * @file node_store_blob_format.h
 * @brief Shared node-store blob format helpers
 */

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#define TRAILMATE_PACK_PUSH __pragma(pack(push, 1))
#define TRAILMATE_PACK_POP __pragma(pack(pop))
#define TRAILMATE_PACKED
#else
#define TRAILMATE_PACK_PUSH
#define TRAILMATE_PACK_POP
#define TRAILMATE_PACKED __attribute__((packed))
#endif

namespace chat
{
namespace contacts
{

enum class NodeBlobValidation
{
    Ok,
    Empty,
    StaleMetadata,
    InvalidLength,
    MissingCrc,
    VersionMismatch,
    CrcMismatch,
    TooManyEntries,
};

TRAILMATE_PACK_PUSH
struct NodeStoreSdHeader
{
    uint8_t ver;
    uint8_t reserved[3];
    uint32_t crc;
    uint32_t count;
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

bool isValidNodeBlobSize(size_t len);
size_t nodeBlobEntryCount(size_t len);
size_t nodeBlobByteSize(size_t count);
size_t nodeBlobEntrySizeForVersion(uint8_t version);
NodeStoreSdHeader makeNodeStoreSdHeader(const uint8_t* data, size_t len);
NodeBlobValidation validateNodeBlobMetadata(size_t len, uint8_t version,
                                            bool has_crc, uint32_t stored_crc,
                                            const uint8_t* data);
NodeBlobValidation validateNodeStoreSdHeader(const NodeStoreSdHeader& header);
NodeBlobValidation validateNodeStoreSdBlob(const NodeStoreSdHeader& header,
                                           const uint8_t* data, size_t len);

} // namespace contacts
} // namespace chat

#undef TRAILMATE_PACK_PUSH
#undef TRAILMATE_PACK_POP
#undef TRAILMATE_PACKED
