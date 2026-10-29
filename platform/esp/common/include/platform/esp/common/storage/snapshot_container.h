#pragma once
#include "hostlink/snapshot_container.h"
#include "platform/esp/common/storage/record_file_io.h"

namespace platform::esp::storage
{
using hostlink::management::snapshot_storage::kEmptySnapshotHeader;
using hostlink::management::snapshot_storage::kSnapshotContainerHeaderBytes;
using hostlink::management::snapshot_storage::kSnapshotContainerMagic;
using hostlink::management::snapshot_storage::kSnapshotContainerReserved;
inline uint16_t snapshotHeaderCrc(uint16_t crc, const uint8_t* data, size_t bytes)
{
    return hostlink::management::snapshot_storage::snapshotHeaderCrc(crc, data, bytes);
}
inline bool snapshotMetadataValid(const hostlink::management::TrackSnapshotMetadata& metadata)
{
    return hostlink::management::snapshot_storage::snapshotMetadataValid(metadata);
}
inline bool writeSnapshotContainerHeader(RecordFileIo& io, RecordFileIo::Handle file,
                                         const hostlink::management::TrackSnapshotMetadata& metadata)
{
    return hostlink::management::snapshot_storage::writeSnapshotContainerHeader(io, file, metadata);
}
inline bool readSnapshotContainerHeader(RecordFileIo& io, RecordFileIo::Handle file,
                                        hostlink::management::TrackSnapshotMetadata& metadata)
{
    return hostlink::management::snapshot_storage::readSnapshotContainerHeader(io, file, metadata);
}
} // namespace platform::esp::storage
