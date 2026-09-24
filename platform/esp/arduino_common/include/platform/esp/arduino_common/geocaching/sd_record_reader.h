#pragma once
#include "geocaching/storage/record_frame.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <algorithm>

namespace platform::esp::arduino_common::geocaching
{
enum class SegmentReadResult : uint8_t { Record, End, Truncated, Corrupt, IoError, WorkspaceTooSmall, InProgress };

struct SdRecordReadCursor
{
    uint8_t* buffer = nullptr;
    uint64_t sequence = 0;
    uint32_t payload_size = 0, payload_read = 0, crc = 0, expected_crc = 0;
    uint8_t header_read = 0;
    ::geocaching::storage::RecordKind kind = ::geocaching::storage::RecordKind::Transaction;
};
static_assert(sizeof(SdRecordReadCursor) <= 64, "Record cursor is metadata, not a payload buffer");

// At most 512 file bytes per invocation, including the header. InProgress MUST
// return to the maintenance owner; callers must not spin until Record. The
// caller keeps the same output buffer across steps. Full-frame storage remains
// an existing decoder constraint, not an allocation performed by this cursor.
inline SegmentReadResult readSdRecord(storage::SdRuntimeFile& file, uint64_t length, uint64_t& offset,
                                      uint8_t* buffer, size_t capacity, SdRecordReadCursor& cursor,
                                      ::geocaching::storage::RecordFrameView& out)
{
    out = {};
    if (!file.is_open()) return SegmentReadResult::IoError;
    if (offset > length) return SegmentReadResult::Corrupt;
    if (!cursor.buffer && offset == length) return SegmentReadResult::End;
    if (!buffer || capacity < 24) return SegmentReadResult::WorkspaceTooSmall;
    if (cursor.buffer && cursor.buffer != buffer) return SegmentReadResult::Corrupt;
    cursor.buffer = buffer;
    size_t budget = 512;
    if (cursor.header_read < 24)
    {
        const size_t needed = 24 - cursor.header_read;
        if (needed > length - offset) return SegmentReadResult::Truncated;
        const int n = file.read(buffer + cursor.header_read, needed);
        if (n == 0) return SegmentReadResult::Truncated;
        if (n < 0 || static_cast<size_t>(n) > needed) return SegmentReadResult::IoError;
        cursor.header_read += static_cast<uint8_t>(n); offset += static_cast<size_t>(n); budget -= static_cast<size_t>(n);
        if (cursor.header_read != 24) return SegmentReadResult::InProgress;
        if (std::memcmp(buffer, "GCR1", 4) || buffer[4] < 2 || buffer[4] > 4 || buffer[5] || buffer[6] || buffer[7] != 24)
            return SegmentReadResult::Corrupt;
        for (unsigned i = 0; i < 4; ++i)
        {
            cursor.payload_size = (cursor.payload_size << 8) | buffer[8 + i];
            cursor.expected_crc = (cursor.expected_crc << 8) | buffer[20 + i];
        }
        for (unsigned i = 0; i < 8; ++i) cursor.sequence = (cursor.sequence << 8) | buffer[12 + i];
        if (!cursor.payload_size || cursor.payload_size > 65536) return SegmentReadResult::Corrupt;
        cursor.kind = static_cast<::geocaching::storage::RecordKind>(buffer[4]);
        cursor.crc = ::sys::crc32(buffer, 20);
    }
    if (cursor.payload_size > capacity - 24) return SegmentReadResult::WorkspaceTooSmall;
    const auto remaining = cursor.payload_size - cursor.payload_read;
    if (remaining > length - offset) return SegmentReadResult::Truncated;
    const size_t requested = std::min<size_t>(budget, remaining);
    auto* destination = buffer + 24 + cursor.payload_read;
    const int n = file.read(destination, requested);
    if (n == 0) return SegmentReadResult::Truncated;
    if (n < 0 || static_cast<size_t>(n) > requested) return SegmentReadResult::IoError;
    cursor.crc = ::sys::crc32(destination, static_cast<size_t>(n), cursor.crc);
    cursor.payload_read += static_cast<uint32_t>(n); offset += static_cast<size_t>(n);
    if (cursor.payload_read != cursor.payload_size) return SegmentReadResult::InProgress;
    if (cursor.crc != cursor.expected_crc) return SegmentReadResult::Corrupt;
    out = {cursor.kind, cursor.sequence, {buffer + 24, cursor.payload_size}};
    cursor = {};
    return SegmentReadResult::Record;
}
} // namespace platform::esp::arduino_common::geocaching
