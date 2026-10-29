#pragma once

#include "hostlink/track_snapshot.h"

namespace hostlink::management::snapshot_storage
{
constexpr uint32_t kSnapshotContainerHeaderBytes = 64;
inline constexpr uint8_t kSnapshotContainerMagic[4] = {'T', 'S', 'N', '1'};
inline constexpr uint8_t kSnapshotContainerReserved[5] = {};
inline constexpr uint8_t kEmptySnapshotHeader[kSnapshotContainerHeaderBytes] = {};

inline uint16_t snapshotHeaderCrc(uint16_t crc, const uint8_t* data, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
    {
        crc ^= uint16_t(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit) crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
inline bool snapshotMetadataValid(const hostlink::management::TrackSnapshotMetadata& metadata)
{
    uint8_t bits = 0;
    for (auto byte : metadata.id) bits |= byte;
    return bits && metadata.bytes && metadata.bytes <= hostlink::management::kTrackSnapshotMaxBytes && metadata.format <= 3;
}

// Fixed container: TSN1 / id16 / sha25632 / length LE32 / format8 /
// reserved-zero5 / CRC16-CCITT LE16, then exact raw recording bytes.
// CRC covers bytes 0..61. This header is not a signature or a content hash.
// Write small fields directly: no automatic 64-byte protocol buffer.
template <typename Io, typename Handle>
inline bool writeSnapshotContainerHeader(Io& io, Handle file,
                                         const hostlink::management::TrackSnapshotMetadata& metadata)
{
    if (!snapshotMetadataValid(metadata) || !io.seek(file, 0)) return false;
    uint16_t crc = 0xffff;
    const auto part = [&](const uint8_t* data, size_t bytes)
    {
        if (io.write(file, data, bytes) != bytes) return false;
        crc = snapshotHeaderCrc(crc, data, bytes);
        return true;
    };
    const uint8_t length[4] = {uint8_t(metadata.bytes), uint8_t(metadata.bytes >> 8), uint8_t(metadata.bytes >> 16), uint8_t(metadata.bytes >> 24)};
    if (!part(kSnapshotContainerMagic, 4) || !part(metadata.id, 16) || !part(metadata.hash, 32) ||
        !part(length, 4) || !part(&metadata.format, 1) || !part(kSnapshotContainerReserved, 5)) return false;
    const uint8_t checksum[2] = {uint8_t(crc), uint8_t(crc >> 8)};
    return io.write(file, checksum, 2) == 2;
}

template <typename Io, typename Handle>
inline bool readSnapshotContainerHeader(Io& io, Handle file,
                                        hostlink::management::TrackSnapshotMetadata& metadata)
{
    metadata = {};
    uint64_t total = 0;
    if (!io.size(file, total) || total <= kSnapshotContainerHeaderBytes ||
        total > kSnapshotContainerHeaderBytes + hostlink::management::kTrackSnapshotMaxBytes || !io.seek(file, 0)) return false;
    uint16_t crc = 0xffff;
    const auto part = [&](uint8_t* data, size_t bytes)
    {
        if (io.read(file, data, bytes) != bytes) return false;
        crc = snapshotHeaderCrc(crc, data, bytes);
        return true;
    };
    uint8_t magic[4], length[4], reserved[5], checksum[2];
    if (!part(magic, 4) || std::memcmp(magic, kSnapshotContainerMagic, 4) || !part(metadata.id, 16) ||
        !part(metadata.hash, 32) || !part(length, 4) || !part(&metadata.format, 1) || !part(reserved, 5) ||
        std::memcmp(reserved, kSnapshotContainerReserved, 5) || io.read(file, checksum, 2) != 2 ||
        crc != (uint16_t(checksum[0]) | uint16_t(checksum[1]) << 8)) return false;
    metadata.bytes = uint32_t(length[0]) | uint32_t(length[1]) << 8 | uint32_t(length[2]) << 16 | uint32_t(length[3]) << 24;
    if (!snapshotMetadataValid(metadata) || total != kSnapshotContainerHeaderBytes + metadata.bytes) return false;
    metadata.sealed = true;
    return true;
}
} // namespace hostlink::management::snapshot_storage
