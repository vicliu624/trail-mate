/**
 * @file node_store_blob_format.cpp
 * @brief Shared node-store blob format helpers
 */

#include "chat/infra/node_store_blob_format.h"

#include "chat/infra/node_store_core.h"

namespace chat
{
namespace contacts
{

bool isValidNodeBlobSize(size_t len)
{
    return (len % NodeStoreCore::kSerializedEntrySizeV8) == 0 ||
           (len % NodeStoreCore::kSerializedEntrySize) == 0 ||
           (len % NodeStoreCore::kLegacySerializedEntrySize) == 0;
}

size_t nodeBlobEntryCount(size_t len)
{
    if ((len % NodeStoreCore::kSerializedEntrySizeV8) == 0)
    {
        return len / NodeStoreCore::kSerializedEntrySizeV8;
    }
    if ((len % NodeStoreCore::kSerializedEntrySize) == 0)
    {
        return len / NodeStoreCore::kSerializedEntrySize;
    }
    return len / NodeStoreCore::kLegacySerializedEntrySize;
}

size_t nodeBlobByteSize(size_t count)
{
    return count * NodeStoreCore::kSerializedEntrySizeV8;
}

NodeStoreSdHeader makeNodeStoreSdHeader(const uint8_t* data, size_t len)
{
    NodeStoreSdHeader header{};
    header.ver = NodeStoreCore::kPersistVersion;
    header.reserved[0] = 0;
    header.reserved[1] = 0;
    header.reserved[2] = 0;
    header.count = static_cast<uint32_t>(nodeBlobEntryCount(len));
    header.crc = NodeStoreCore::computeBlobCrc(data, len);
    return header;
}

NodeBlobValidation validateNodeBlobMetadata(size_t len, uint8_t version,
                                            bool has_crc, uint32_t stored_crc,
                                            const uint8_t* data)
{
    if (len == 0)
    {
        return has_crc ? NodeBlobValidation::StaleMetadata : NodeBlobValidation::Empty;
    }
    if (!isValidNodeBlobSize(len))
    {
        return NodeBlobValidation::InvalidLength;
    }
    const size_t entry_size = (version == NodeStoreCore::kPersistVersion)
                                  ? NodeStoreCore::kSerializedEntrySizeV8
                                  : ((version == (NodeStoreCore::kPersistVersion - 1))
                                         ? NodeStoreCore::kSerializedEntrySize
                                         : NodeStoreCore::kLegacySerializedEntrySize);
    if ((len % entry_size) != 0)
    {
        return NodeBlobValidation::InvalidLength;
    }
    if (nodeBlobEntryCount(len) > NodeStoreCore::kMaxNodes)
    {
        return NodeBlobValidation::TooManyEntries;
    }
    if (!has_crc)
    {
        return NodeBlobValidation::MissingCrc;
    }
    if (version != NodeStoreCore::kPersistVersion &&
        version != (NodeStoreCore::kPersistVersion - 1))
    {
        return NodeBlobValidation::VersionMismatch;
    }
    if (!data)
    {
        return NodeBlobValidation::InvalidLength;
    }
    const uint32_t calc_crc = NodeStoreCore::computeBlobCrc(data, len);
    return (calc_crc == stored_crc) ? NodeBlobValidation::Ok : NodeBlobValidation::CrcMismatch;
}

NodeBlobValidation validateNodeStoreSdHeader(const NodeStoreSdHeader& header)
{
    if (header.ver != NodeStoreCore::kPersistVersion &&
        header.ver != (NodeStoreCore::kPersistVersion - 1))
    {
        return NodeBlobValidation::VersionMismatch;
    }
    if (header.count > NodeStoreCore::kMaxNodes)
    {
        return NodeBlobValidation::TooManyEntries;
    }
    return NodeBlobValidation::Ok;
}

NodeBlobValidation validateNodeStoreSdBlob(const NodeStoreSdHeader& header,
                                           const uint8_t* data, size_t len)
{
    const NodeBlobValidation header_status = validateNodeStoreSdHeader(header);
    if (header_status != NodeBlobValidation::Ok)
    {
        return header_status;
    }
    const size_t entry_size = (header.ver == NodeStoreCore::kPersistVersion)
                                  ? NodeStoreCore::kSerializedEntrySizeV8
                                  : ((header.ver == (NodeStoreCore::kPersistVersion - 1))
                                         ? NodeStoreCore::kSerializedEntrySize
                                         : NodeStoreCore::kLegacySerializedEntrySize);
    const size_t expected_bytes = header.count * entry_size;
    if (expected_bytes != len || !isValidNodeBlobSize(len))
    {
        return NodeBlobValidation::InvalidLength;
    }
    const uint32_t calc_crc = NodeStoreCore::computeBlobCrc(data, len);
    return (calc_crc == header.crc) ? NodeBlobValidation::Ok : NodeBlobValidation::CrcMismatch;
}

} // namespace contacts
} // namespace chat
