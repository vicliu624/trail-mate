#pragma once
#include "geocaching/domain/record.h"
#include "sys/crc32.h"
#include <array>
#include <cstring>

namespace geocaching::storage
{
enum class RecordKind : uint8_t
{
    Transaction = 2,
    CheckpointPage = 3,
    CheckpointTail = 4
};
using RecordHeader = std::array<uint8_t, 24>;
struct RecordFrameView
{
    RecordKind kind = RecordKind::Transaction;
    uint64_t sequence = 0;
    ByteView payload;
};

// CRC is filled only after the caller has consumed all payload spans.
inline bool makeRecordPrefix(RecordKind kind, uint64_t sequence, size_t payload_size, RecordHeader& out)
{
    out = {};
    const auto tag = static_cast<uint8_t>(kind);
    if (tag < 2 || tag > 4 || payload_size == 0 || payload_size > 65536) return false;
    out[0] = 'G';
    out[1] = 'C';
    out[2] = 'R';
    out[3] = '1';
    out[4] = tag;
    out[7] = 24;
    for (unsigned i = 0; i < 4; ++i) out[8 + i] = static_cast<uint8_t>(payload_size >> ((3 - i) * 8));
    for (unsigned i = 0; i < 8; ++i) out[12 + i] = static_cast<uint8_t>(sequence >> ((7 - i) * 8));
    return true;
}

inline void finishRecordHeader(RecordHeader& out, uint32_t crc)
{
    for (unsigned i = 0; i < 4; ++i) out[20 + i] = static_cast<uint8_t>(crc >> ((3 - i) * 8));
}

inline bool makeRecordHeader(RecordKind kind, uint64_t sequence, ByteView payload, RecordHeader& out)
{
    out = {};
    if (!payload.data || !makeRecordPrefix(kind, sequence, payload.size, out)) return false;
    finishRecordHeader(out, sys::crc32(payload.data, payload.size, sys::crc32(out.data(), 20)));
    return true;
}

// One complete frame only. No allocation based on disk-controlled lengths.
// CRC is corruption detection; payload schema and sequence-chain validation
// must still succeed before a recovered transaction can be applied.
inline bool decodeRecordFrame(ByteView bytes, RecordFrameView& out)
{
    out = {};
    if (!bytes.data || bytes.size < 24 || bytes.size > 65560) return false;
    const auto* h = bytes.data;
    if (std::memcmp(h, "GCR1", 4) || h[4] < 2 || h[4] > 4 || h[5] || h[6] || h[7] != 24) return false;
    uint32_t length = 0, stored_crc = 0;
    uint64_t sequence = 0;
    for (unsigned i = 0; i < 4; ++i) length = (length << 8) | h[8 + i];
    if (length == 0 || length > 65536 || bytes.size - 24 != length) return false;
    for (unsigned i = 0; i < 8; ++i) sequence = (sequence << 8) | h[12 + i];
    for (unsigned i = 0; i < 4; ++i) stored_crc = (stored_crc << 8) | h[20 + i];
    const ByteView payload{h + 24, length};
    if (sys::crc32(payload.data, payload.size, sys::crc32(h, 20)) != stored_crc) return false;
    out = {static_cast<RecordKind>(h[4]), sequence, payload};
    return true;
}
} // namespace geocaching::storage
