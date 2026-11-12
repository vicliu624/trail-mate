#pragma once

#include "hostlink_types.h"
#include <cstdint>
#include <cstring>

namespace hostlink::management
{
constexpr uint8_t kVersion = 1;
constexpr size_t kIdBytes = 16;

enum class Operation : uint16_t
{
    Capabilities = 0,
    AuthorInfo = 1,
    AuthorProve = 2,
    SessionClose = 3,
    IdentityBackupExport = 4,
    IdentityBackupRestore = 5,
    IdentityRestoreFormats = 6,
    RadioIdentityInfo = 7,
    RadioIdentityBackupExport = 8,
    RadioIdentityBackupRestore = 9,
    EnterBootloader = 10,
    ConfigSchema = 16,
    ConfigSnapshot = 17,
    ConfigBegin = 18,
    ConfigPatch = 19,
    ConfigValidate = 20,
    ConfigCommit = 21,
    ConfigAbort = 22,
    ConfigCommitStatus = 23,
    TransferBegin = 32,
    TransferChunk = 33,
    TransferStatus = 34,
    TransferFinish = 35,
    TransferAbort = 36,
    TransferCleanup = 37,
    TransferListOwned = 38,
    JobGet = 48,
    JobList = 49,
    JobCancel = 50,
    CacheListOwned = 64,
    CacheDraftGet = 65,
    CacheDraftPut = 66,
    CachePrepare = 67,
    CacheCommit = 68,
    CacheHistory = 69,
    CacheDirectoryList = 70,
    CacheSignBegin = 71,
    CacheSignStatus = 72,
    CacheSignRead = 73,
    CacheSignRelease = 74,
    CacheConfirmBegin = 75,
    CacheConfirmStatus = 76,
    CacheConfirmRelease = 77,
    TrackList = 80,
    TrackSnapshot = 81,
    TrackPrepare = 82,
    TrackCommit = 83,
    TrackHistory = 84,
    RouteInstall = 85,
    TrackSnapshotBuild = 86,
    TrackSnapshotCancel = 87,
    TrackSnapshotRelease = 88,
    TrackDirectoryList = 89,
    TrackDirectoryDiscover = 90,
    TrackReadBegin = 91,
    TrackReadStatus = 92,
    TrackReadChunk = 93,
    TrackReadRelease = 94,
    RouteInstallStatus = 95,
    TrackSignPrepare = 96,
    TrackConfirmPublication = 97,
    TrackImportHistory = 98,
    TrackPackageSignPrepare = 99,
    TrackPackageInstall = 100,
    TrackPackageInstallStatus = 101,
    ChatCapabilities = 112,
    ChatSend = 113,
    ChatSendGet = 114,
};

enum class Status : uint16_t
{
    Ok = 0,
    Unsupported = 1,
    Unauthorized = 2,
    InvalidField = 3,
    Conflict = 4,
    Busy = 5,
    StorageUnavailable = 6,
    LimitExceeded = 7,
    HashMismatch = 8,
    StaleIdentity = 9,
    Expired = 10,
    Internal = 11,
};

// Borrowed spans: the frame owner must keep the backing bytes until dispatch
// completes. No queues may retain these pointers after the frame is reused.
struct EnvelopeView
{
    const uint8_t* session = nullptr;
    const uint8_t* request = nullptr;
    uint16_t code = 0;
    const uint8_t* body = nullptr;
    size_t body_size = 0;
};

namespace detail
{
class Cursor
{
  public:
    Cursor(const uint8_t* bytes, size_t size) : bytes_(bytes), size_(size) {}
    size_t position() const { return offset_; }
    bool byte(uint8_t& out)
    {
        if (offset_ >= size_) return false;
        out = bytes_[offset_++];
        return true;
    }
    bool number(unsigned width, uint64_t& out)
    {
        if (width > size_ - offset_) return false;
        out = 0;
        for (unsigned i = 0; i < width; ++i) out = (out << 8) | bytes_[offset_++];
        return true;
    }
    bool unsignedValue(uint64_t& out)
    {
        uint8_t tag;
        if (!byte(tag)) return false;
        if (tag <= 127)
        {
            out = tag;
            return true;
        }
        if (tag < 0xcc || tag > 0xcf) return false;
        const unsigned width = 1U << (tag - 0xcc);
        return number(width, out) && out >= (width == 1 ? 128ULL : 1ULL << (width * 4));
    }
    bool id(const uint8_t*& out)
    {
        uint8_t tag, length;
        if (!byte(tag) || tag != 0xc4 || !byte(length) || length != kIdBytes || size_ - offset_ < kIdBytes) return false;
        out = bytes_ + offset_;
        offset_ += kIdBytes;
        return true;
    }
    bool object(unsigned depth = 0)
    {
        if (depth > 8 || ++elements_ > 128) return false;
        uint8_t tag;
        if (!byte(tag)) return false;
        if (tag <= 127 || tag >= 0xe0 || tag == 0xc0 || tag == 0xc2 || tag == 0xc3) return true;
        uint64_t n = 0;
        if (tag >= 0xcc && tag <= 0xcf)
        {
            const unsigned width = 1U << (tag - 0xcc);
            return number(width, n) && n >= (width == 1 ? 128ULL : 1ULL << (width * 4));
        }
        if (tag >= 0xd0 && tag <= 0xd3)
        {
            const unsigned width = 1U << (tag - 0xd0);
            if (!number(width, n) || !(n & (1ULL << (width * 8 - 1)))) return false;
            const uint64_t mask = width == 8 ? UINT64_MAX : (1ULL << (width * 8)) - 1;
            const int64_t value = -1 - static_cast<int64_t>((~n) & mask);
            return value < (width == 1 ? -32LL : -(1LL << (width * 4 - 1)));
        }
        if ((tag & 0xf0) == 0x90) n = tag & 15;
        else if (tag == 0xdc || tag == 0xdd)
        {
            if (!number(tag == 0xdc ? 2 : 4, n) || n < (tag == 0xdc ? 16ULL : 65536ULL)) return false;
        }
        else
        {
            bool text = false;
            if ((tag & 0xe0) == 0xa0)
            {
                text = true;
                n = tag & 31;
            }
            else if (tag >= 0xd9 && tag <= 0xdb)
            {
                text = true;
                const unsigned width = 1U << (tag - 0xd9);
                if (!number(width, n) || n < (width == 1 ? 32ULL : 1ULL << (width * 4))) return false;
            }
            else if (tag >= 0xc4 && tag <= 0xc6)
            {
                const unsigned width = 1U << (tag - 0xc4);
                if (!number(width, n) || (width > 1 && n < (1ULL << (width * 4)))) return false;
            }
            else return false; // maps, floats, bool and extension objects are not CMP1.
            if (n > size_ - offset_) return false;
            if (text && !utf8(bytes_ + offset_, static_cast<size_t>(n))) return false;
            offset_ += static_cast<size_t>(n);
            return true;
        }
        if (n > 128 - elements_) return false;
        for (uint64_t i = 0; i < n; ++i)
            if (!object(depth + 1)) return false;
        return true;
    }

  private:
    static bool utf8(const uint8_t* bytes, size_t size)
    {
        size_t i = 0;
        while (i < size)
        {
            const uint8_t first = bytes[i++];
            if (first < 128) continue;
            const unsigned count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2
                                                                    : first >= 0xf0 && first <= 0xf4   ? 3
                                                                                                       : 0;
            if (!count || size - i < count) return false;
            if ((first == 0xe0 && bytes[i] < 0xa0) || (first == 0xed && bytes[i] >= 0xa0) ||
                (first == 0xf0 && bytes[i] < 0x90) || (first == 0xf4 && bytes[i] >= 0x90)) return false;
            for (unsigned j = 0; j < count; ++j)
                if ((bytes[i++] & 0xc0) != 0x80) return false;
        }
        return true;
    }
    const uint8_t* bytes_;
    size_t size_, offset_ = 0, elements_ = 0;
};
} // namespace detail

inline bool decodeEnvelope(const uint8_t* bytes, size_t size, EnvelopeView& out)
{
    out = {};
    if (!bytes || size > kMaxFrameLen) return false;
    detail::Cursor cursor(bytes, size);
    uint8_t array, version;
    uint64_t code;
    EnvelopeView candidate;
    if (!cursor.byte(array) || array != 0x95 || !cursor.byte(version) || version != kVersion ||
        !cursor.id(candidate.session) || !cursor.id(candidate.request) ||
        !cursor.unsignedValue(code) || code > UINT16_MAX) return false;
    candidate.code = static_cast<uint16_t>(code);
    candidate.body = bytes + cursor.position();
    candidate.body_size = size - cursor.position();
    if (!cursor.object() || cursor.position() != size) return false;
    out = candidate;
    return true;
}

inline bool encodeEnvelope(const uint8_t session[kIdBytes], const uint8_t request[kIdBytes],
                           uint16_t code, const uint8_t* body, size_t body_size,
                           uint8_t* out, size_t capacity, size_t& written)
{
    written = 0;
    const size_t prefix = 38 + (code <= 127 ? 1 : code <= 255 ? 2
                                                              : 3);
    if (!session || !request || !body || !out || body_size > kMaxFrameLen - prefix || capacity < prefix + body_size) return false;
    detail::Cursor cursor(body, body_size);
    if (!cursor.object() || cursor.position() != body_size) return false;
    // IDs must be independent of the output arena; body may overlap it.
    std::memmove(out + prefix, body, body_size);
    out[0] = 0x95;
    out[1] = kVersion;
    out[2] = 0xc4;
    out[3] = kIdBytes;
    std::memcpy(out + 4, session, kIdBytes);
    out[20] = 0xc4;
    out[21] = kIdBytes;
    std::memcpy(out + 22, request, kIdBytes);
    if (code <= 127) out[38] = static_cast<uint8_t>(code);
    else if (code <= 255)
    {
        out[38] = 0xcc;
        out[39] = static_cast<uint8_t>(code);
    }
    else
    {
        out[38] = 0xcd;
        out[39] = static_cast<uint8_t>(code >> 8);
        out[40] = static_cast<uint8_t>(code);
    }
    written = prefix + body_size;
    return true;
}
} // namespace hostlink::management
