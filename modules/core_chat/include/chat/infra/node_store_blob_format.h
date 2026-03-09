/**
 * @file node_store_blob_format.h
 * @brief Shared node-store blob format helpers
 */

#pragma once

#include <cstddef>
#include <cstdint>

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

struct NodeStoreSdHeader
{
    uint8_t ver;
    uint8_t reserved[3];
    uint32_t crc;
    uint32_t count;
} __attribute__((packed));

bool isValidNodeBlobSize(size_t len);
size_t nodeBlobEntryCount(size_t len);
size_t nodeBlobByteSize(size_t count);
NodeStoreSdHeader makeNodeStoreSdHeader(const uint8_t* data, size_t len);
NodeBlobValidation validateNodeBlobMetadata(size_t len, uint8_t version,
                                            bool has_crc, uint32_t stored_crc,
                                            const uint8_t* data);
NodeBlobValidation validateNodeStoreSdHeader(const NodeStoreSdHeader& header);
NodeBlobValidation validateNodeStoreSdBlob(const NodeStoreSdHeader& header,
                                           const uint8_t* data, size_t len);

} // namespace contacts
} // namespace chat