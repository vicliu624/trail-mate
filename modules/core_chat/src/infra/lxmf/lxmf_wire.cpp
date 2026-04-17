/**
 * @file lxmf_wire.cpp
 * @brief Shared LXMF wire helpers for direct text/app-data subsets
 */

#include "chat/infra/lxmf/lxmf_wire.h"

#include <algorithm>
#include <cstring>

namespace chat::lxmf
{
namespace
{
constexpr uint8_t kAppPayloadMagic[4] = {'T', 'M', 'A', 'P'};
constexpr uint8_t kAppPayloadVersion = 1;
constexpr uint8_t kAppPayloadFlagWantResponse = 0x01;
constexpr size_t kAppPayloadHeaderLen = 18;

struct Cursor
{
    const uint8_t* data = nullptr;
    size_t len = 0;
    size_t pos = 0;
};

bool appendByte(uint8_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (!out || used >= out_len)
    {
        return false;
    }
    out[used++] = value;
    return true;
}

bool appendBytes(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if ((!data && len != 0) || !out || used + len > out_len)
    {
        return false;
    }
    if (len != 0)
    {
        memcpy(out + used, data, len);
    }
    used += len;
    return true;
}

bool appendArrayHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendByte(static_cast<uint8_t>(0x90U | (count & 0x0FU)), out, out_len, used);
    }
    return appendByte(0xDC, out, out_len, used) &&
           appendByte(0x00, out, out_len, used) &&
           appendByte(count, out, out_len, used);
}

bool appendMapHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendByte(static_cast<uint8_t>(0x80U | (count & 0x0FU)), out, out_len, used);
    }
    return appendByte(0xDE, out, out_len, used) &&
           appendByte(0x00, out, out_len, used) &&
           appendByte(count, out, out_len, used);
}

bool appendNil(uint8_t* out, size_t out_len, size_t& used)
{
    return appendByte(0xC0, out, out_len, used);
}

bool appendBool(bool value, uint8_t* out, size_t out_len, size_t& used)
{
    return appendByte(value ? 0xC3 : 0xC2, out, out_len, used);
}

bool appendUint(uint32_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (value <= 0x7FU)
    {
        return appendByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFU)
    {
        return appendByte(0xCC, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFFFU)
    {
        return appendByte(0xCD, out, out_len, used) &&
               appendByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
               appendByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
    }
    return appendByte(0xCE, out, out_len, used) &&
           appendByte(static_cast<uint8_t>((value >> 24) & 0xFFU), out, out_len, used) &&
           appendByte(static_cast<uint8_t>((value >> 16) & 0xFFU), out, out_len, used) &&
           appendByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
           appendByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
}

bool appendFloat64(double value, uint8_t* out, size_t out_len, size_t& used)
{
    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    bits.d = value;

    if (!appendByte(0xCB, out, out_len, used))
    {
        return false;
    }
    for (int i = 7; i >= 0; --i)
    {
        if (!appendByte(bits.b[i], out, out_len, used))
        {
            return false;
        }
    }
    return true;
}

bool appendBin(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if (len <= 0xFFU)
    {
        return appendByte(0xC4, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(len), out, out_len, used) &&
               appendBytes(data, len, out, out_len, used);
    }
    if (len <= 0xFFFFU)
    {
        return appendByte(0xC5, out, out_len, used) &&
               appendByte(static_cast<uint8_t>((len >> 8) & 0xFFU), out, out_len, used) &&
               appendByte(static_cast<uint8_t>(len & 0xFFU), out, out_len, used) &&
               appendBytes(data, len, out, out_len, used);
    }
    return false;
}

bool readByte(Cursor& cursor, uint8_t* out)
{
    if (!out || !cursor.data || cursor.pos >= cursor.len)
    {
        return false;
    }
    *out = cursor.data[cursor.pos++];
    return true;
}

bool peekByte(const Cursor& cursor, uint8_t* out)
{
    if (!out || !cursor.data || cursor.pos >= cursor.len)
    {
        return false;
    }
    *out = cursor.data[cursor.pos];
    return true;
}

bool readArrayHeader(Cursor& cursor, size_t* out_count)
{
    uint8_t tag = 0;
    if (!readByte(cursor, &tag) || !out_count)
    {
        return false;
    }
    if ((tag & 0xF0U) == 0x90U)
    {
        *out_count = static_cast<size_t>(tag & 0x0FU);
        return true;
    }
    if (tag == 0xDC)
    {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!readByte(cursor, &hi) || !readByte(cursor, &lo))
        {
            return false;
        }
        *out_count = static_cast<size_t>((static_cast<uint16_t>(hi) << 8) | lo);
        return true;
    }
    return false;
}

bool readMapHeader(Cursor& cursor, size_t* out_count)
{
    uint8_t tag = 0;
    if (!readByte(cursor, &tag) || !out_count)
    {
        return false;
    }
    if ((tag & 0xF0U) == 0x80U)
    {
        *out_count = static_cast<size_t>(tag & 0x0FU);
        return true;
    }
    if (tag == 0xDE)
    {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!readByte(cursor, &hi) || !readByte(cursor, &lo))
        {
            return false;
        }
        *out_count = static_cast<size_t>((static_cast<uint16_t>(hi) << 8) | lo);
        return true;
    }
    return false;
}

bool readUint(Cursor& cursor, uint32_t* out_value)
{
    uint8_t tag = 0;
    if (!readByte(cursor, &tag) || !out_value)
    {
        return false;
    }
    if (tag <= 0x7F)
    {
        *out_value = tag;
        return true;
    }
    if (tag == 0xCC)
    {
        uint8_t value = 0;
        if (!readByte(cursor, &value))
        {
            return false;
        }
        *out_value = value;
        return true;
    }
    if (tag == 0xCD)
    {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!readByte(cursor, &hi) || !readByte(cursor, &lo))
        {
            return false;
        }
        *out_value = (static_cast<uint32_t>(hi) << 8) | lo;
        return true;
    }
    if (tag == 0xCE)
    {
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        uint8_t b3 = 0;
        if (!readByte(cursor, &b0) || !readByte(cursor, &b1) ||
            !readByte(cursor, &b2) || !readByte(cursor, &b3))
        {
            return false;
        }
        *out_value = (static_cast<uint32_t>(b0) << 24) |
                     (static_cast<uint32_t>(b1) << 16) |
                     (static_cast<uint32_t>(b2) << 8) |
                     static_cast<uint32_t>(b3);
        return true;
    }
    return false;
}

bool readFloat64(Cursor& cursor, double* out_value)
{
    uint8_t tag = 0;
    if (!readByte(cursor, &tag) || tag != 0xCB || !out_value || cursor.pos + 8 > cursor.len)
    {
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    for (int i = 7; i >= 0; --i)
    {
        bits.b[i] = cursor.data[cursor.pos++];
    }
    *out_value = bits.d;
    return true;
}

bool readNil(Cursor& cursor)
{
    uint8_t tag = 0;
    return readByte(cursor, &tag) && tag == 0xC0;
}

bool readBool(Cursor& cursor, bool* out_value)
{
    uint8_t tag = 0;
    if (!readByte(cursor, &tag) || !out_value)
    {
        return false;
    }
    if (tag == 0xC2)
    {
        *out_value = false;
        return true;
    }
    if (tag == 0xC3)
    {
        *out_value = true;
        return true;
    }
    return false;
}

void writeU32Be(uint32_t value, uint8_t* out)
{
    if (!out)
    {
        return;
    }
    out[0] = static_cast<uint8_t>((value >> 24) & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    out[3] = static_cast<uint8_t>(value & 0xFFU);
}

uint32_t readU32Be(const uint8_t* data)
{
    if (!data)
    {
        return 0;
    }
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

bool readBinary(Cursor& cursor, std::vector<uint8_t>* out_data)
{
    if (!out_data)
    {
        return false;
    }

    uint8_t tag = 0;
    if (!readByte(cursor, &tag))
    {
        return false;
    }

    size_t len = 0;
    if (tag == 0xC4)
    {
        uint8_t len8 = 0;
        if (!readByte(cursor, &len8))
        {
            return false;
        }
        len = len8;
    }
    else if (tag == 0xC5)
    {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!readByte(cursor, &hi) || !readByte(cursor, &lo))
        {
            return false;
        }
        len = static_cast<size_t>((static_cast<uint16_t>(hi) << 8) | lo);
    }
    else if ((tag & 0xE0U) == 0xA0U)
    {
        len = static_cast<size_t>(tag & 0x1FU);
    }
    else if (tag == 0xD9)
    {
        uint8_t len8 = 0;
        if (!readByte(cursor, &len8))
        {
            return false;
        }
        len = len8;
    }
    else
    {
        return false;
    }

    if (cursor.pos + len > cursor.len)
    {
        return false;
    }

    out_data->assign(cursor.data + cursor.pos, cursor.data + cursor.pos + len);
    cursor.pos += len;
    return true;
}

bool skipObject(Cursor& cursor)
{
    uint8_t tag = 0;
    if (!peekByte(cursor, &tag))
    {
        return false;
    }

    if (tag == 0xC0)
    {
        return readNil(cursor);
    }
    if (tag == 0xC2 || tag == 0xC3)
    {
        bool ignored = false;
        return readBool(cursor, &ignored);
    }
    if (tag == 0xCB)
    {
        double ignored = 0.0;
        return readFloat64(cursor, &ignored);
    }
    if (tag == 0xCC || tag == 0xCD || tag == 0xCE || tag <= 0x7F)
    {
        uint32_t ignored = 0;
        return readUint(cursor, &ignored);
    }
    if ((tag & 0xF0U) == 0x80U || tag == 0xDE)
    {
        size_t count = 0;
        if (!readMapHeader(cursor, &count))
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            if (!skipObject(cursor) || !skipObject(cursor))
            {
                return false;
            }
        }
        return true;
    }
    if ((tag & 0xF0U) == 0x90U || tag == 0xDC)
    {
        size_t count = 0;
        if (!readArrayHeader(cursor, &count))
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            if (!skipObject(cursor))
            {
                return false;
            }
        }
        return true;
    }

    std::vector<uint8_t> ignored;
    return readBinary(cursor, &ignored);
}

bool captureObjectBytes(Cursor& cursor, std::vector<uint8_t>* out_data)
{
    if (!out_data || !cursor.data || cursor.pos > cursor.len)
    {
        return false;
    }

    const size_t start = cursor.pos;
    if (!skipObject(cursor) || cursor.pos < start || cursor.pos > cursor.len)
    {
        return false;
    }

    out_data->assign(cursor.data + start, cursor.data + cursor.pos);
    return true;
}

bool appendArrayOfBins(const std::vector<std::vector<uint8_t>>& items,
                       uint8_t* out,
                       size_t out_len,
                       size_t& used)
{
    if (!appendArrayHeader(static_cast<uint8_t>(items.size()), out, out_len, used))
    {
        return false;
    }

    for (const auto& item : items)
    {
        if (!appendBin(item.data(), item.size(), out, out_len, used))
        {
            return false;
        }
    }

    return true;
}

bool readArrayOfBins(Cursor& cursor, std::vector<std::vector<uint8_t>>* out_items)
{
    if (!out_items)
    {
        return false;
    }

    size_t count = 0;
    if (!readArrayHeader(cursor, &count))
    {
        return false;
    }

    std::vector<std::vector<uint8_t>> items;
    items.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        std::vector<uint8_t> item;
        if (!readBinary(cursor, &item))
        {
            return false;
        }
        items.push_back(std::move(item));
    }

    *out_items = std::move(items);
    return true;
}

} // namespace

bool packPeerAnnounceAppData(const char* display_name,
                             bool has_stamp_cost,
                             uint8_t stamp_cost,
                             uint8_t* out_data,
                             size_t* inout_len)
{
    if (!out_data || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    const uint8_t* name_bytes = reinterpret_cast<const uint8_t*>(display_name ? display_name : "");
    const size_t name_len = (display_name != nullptr) ? strlen(display_name) : 0;

    if (!appendArrayHeader(2, out_data, *inout_len, used))
    {
        return false;
    }
    if (name_len == 0)
    {
        if (!appendNil(out_data, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendBin(name_bytes, name_len, out_data, *inout_len, used))
    {
        return false;
    }

    if (has_stamp_cost)
    {
        if (!appendUint(stamp_cost, out_data, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendNil(out_data, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool unpackPeerAnnounceAppData(const uint8_t* data, size_t len,
                               char* out_display_name, size_t display_name_len,
                               bool* out_has_stamp_cost,
                               uint8_t* out_stamp_cost)
{
    if (!data || len == 0 || !out_display_name || display_name_len == 0)
    {
        return false;
    }

    out_display_name[0] = '\0';
    if (out_has_stamp_cost)
    {
        *out_has_stamp_cost = false;
    }
    if (out_stamp_cost)
    {
        *out_stamp_cost = 0;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;
    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count != 2)
    {
        return false;
    }

    uint8_t next = 0;
    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        if (!readNil(cursor))
        {
            return false;
        }
    }
    else
    {
        std::vector<uint8_t> name;
        if (!readBinary(cursor, &name))
        {
            return false;
        }
        const size_t copy_len = std::min(name.size(), display_name_len - 1);
        memcpy(out_display_name, name.data(), copy_len);
        out_display_name[copy_len] = '\0';
    }

    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        return readNil(cursor);
    }

    uint32_t stamp = 0;
    if (!readUint(cursor, &stamp))
    {
        return false;
    }
    if (out_has_stamp_cost)
    {
        *out_has_stamp_cost = true;
    }
    if (out_stamp_cost)
    {
        *out_stamp_cost = static_cast<uint8_t>(stamp);
    }
    return true;
}

bool encodeTextPayload(double timestamp,
                       const char* title,
                       const char* content,
                       uint8_t* out_payload,
                       size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    const uint8_t* title_bytes = reinterpret_cast<const uint8_t*>(title ? title : "");
    const size_t title_len = (title != nullptr) ? strlen(title) : 0;
    const uint8_t* content_bytes = reinterpret_cast<const uint8_t*>(content ? content : "");
    const size_t content_len = (content != nullptr) ? strlen(content) : 0;

    size_t used = 0;
    if (!appendArrayHeader(4, out_payload, *inout_len, used) ||
        !appendFloat64(timestamp, out_payload, *inout_len, used) ||
        !appendBin(title_bytes, title_len, out_payload, *inout_len, used) ||
        !appendBin(content_bytes, content_len, out_payload, *inout_len, used) ||
        !appendMapHeader(0, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool encodeAppDataPayload(uint32_t portnum,
                          uint32_t packet_id,
                          uint32_t request_id,
                          bool want_response,
                          const uint8_t* payload,
                          size_t payload_len,
                          uint8_t* out_payload,
                          size_t* inout_len)
{
    if (!out_payload || !inout_len || (!payload && payload_len != 0))
    {
        return false;
    }

    // Keep business payloads self-describing so the adapter can branch after
    // outer LXMF decryption and signature verification.
    const size_t required_len = kAppPayloadHeaderLen + payload_len;
    if (*inout_len < required_len)
    {
        *inout_len = required_len;
        return false;
    }

    memcpy(out_payload, kAppPayloadMagic, sizeof(kAppPayloadMagic));
    out_payload[4] = kAppPayloadVersion;
    out_payload[5] = want_response ? kAppPayloadFlagWantResponse : 0;
    writeU32Be(portnum, out_payload + 6);
    writeU32Be(packet_id, out_payload + 10);
    writeU32Be(request_id, out_payload + 14);
    if (payload_len != 0)
    {
        memcpy(out_payload + kAppPayloadHeaderLen, payload, payload_len);
    }

    *inout_len = required_len;
    return true;
}

bool encodeLinkRequestPayload(double requested_at,
                              const uint8_t path_hash[reticulum::kTruncatedHashSize],
                              const uint8_t* packed_data,
                              size_t packed_data_len,
                              bool data_is_nil,
                              uint8_t* out_payload,
                              size_t* inout_len)
{
    if (!path_hash || !out_payload || !inout_len || (!data_is_nil && !packed_data && packed_data_len != 0))
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(3, out_payload, *inout_len, used) ||
        !appendFloat64(requested_at, out_payload, *inout_len, used) ||
        !appendBin(path_hash, reticulum::kTruncatedHashSize, out_payload, *inout_len, used))
    {
        return false;
    }

    if (data_is_nil)
    {
        if (!appendNil(out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendBytes(packed_data, packed_data_len, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodeLinkRequestPayload(const uint8_t* data, size_t len, DecodedLinkRequest* out_payload)
{
    if (!data || len == 0 || !out_payload)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;
    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count != 3)
    {
        return false;
    }

    DecodedLinkRequest decoded{};
    if (!readFloat64(cursor, &decoded.requested_at))
    {
        return false;
    }

    std::vector<uint8_t> path_hash;
    if (!readBinary(cursor, &path_hash) || path_hash.size() != reticulum::kTruncatedHashSize)
    {
        return false;
    }
    memcpy(decoded.path_hash, path_hash.data(), path_hash.size());

    uint8_t next = 0;
    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        if (!readNil(cursor))
        {
            return false;
        }
        decoded.data_is_nil = true;
    }
    else if (!captureObjectBytes(cursor, &decoded.packed_data))
    {
        return false;
    }

    *out_payload = std::move(decoded);
    return true;
}

bool encodeLinkResponsePayload(const uint8_t* request_id,
                               size_t request_id_len,
                               const uint8_t* packed_data,
                               size_t packed_data_len,
                               bool data_is_nil,
                               uint8_t* out_payload,
                               size_t* inout_len)
{
    if (!request_id || request_id_len == 0 || !out_payload || !inout_len ||
        (!data_is_nil && !packed_data && packed_data_len != 0))
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(2, out_payload, *inout_len, used) ||
        !appendBin(request_id, request_id_len, out_payload, *inout_len, used))
    {
        return false;
    }

    if (data_is_nil)
    {
        if (!appendNil(out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendBytes(packed_data, packed_data_len, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodeLinkResponsePayload(const uint8_t* data, size_t len, DecodedLinkResponse* out_payload)
{
    if (!data || len == 0 || !out_payload)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;
    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count != 2)
    {
        return false;
    }

    DecodedLinkResponse decoded{};
    if (!readBinary(cursor, &decoded.request_id) || decoded.request_id.empty())
    {
        return false;
    }

    uint8_t next = 0;
    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        if (!readNil(cursor))
        {
            return false;
        }
        decoded.data_is_nil = true;
    }
    else if (!captureObjectBytes(cursor, &decoded.packed_data))
    {
        return false;
    }

    *out_payload = std::move(decoded);
    return true;
}

bool encodeResourceAdvertisement(uint32_t transfer_size,
                                 uint32_t data_size,
                                 uint32_t part_count,
                                 const uint8_t resource_hash[reticulum::kFullHashSize],
                                 const uint8_t random_hash[4],
                                 const uint8_t original_hash[reticulum::kFullHashSize],
                                 uint32_t segment_index,
                                 uint32_t total_segments,
                                 const uint8_t* request_id,
                                 size_t request_id_len,
                                 uint8_t flags,
                                 const uint8_t* hashmap,
                                 size_t hashmap_len,
                                 uint8_t* out_payload,
                                 size_t* inout_len)
{
    if (!resource_hash || !random_hash || !original_hash || !hashmap || hashmap_len == 0 ||
        !out_payload || !inout_len || (request_id_len != 0 && !request_id))
    {
        return false;
    }

    size_t used = 0;
    if (!appendMapHeader(10, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("t"), 1, out_payload, *inout_len, used) ||
        !appendUint(transfer_size, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("d"), 1, out_payload, *inout_len, used) ||
        !appendUint(data_size, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("n"), 1, out_payload, *inout_len, used) ||
        !appendUint(part_count, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("h"), 1, out_payload, *inout_len, used) ||
        !appendBin(resource_hash, reticulum::kFullHashSize, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("r"), 1, out_payload, *inout_len, used) ||
        !appendBin(random_hash, 4, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("o"), 1, out_payload, *inout_len, used) ||
        !appendBin(original_hash, reticulum::kFullHashSize, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("i"), 1, out_payload, *inout_len, used) ||
        !appendUint(segment_index, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("l"), 1, out_payload, *inout_len, used) ||
        !appendUint(total_segments, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("q"), 1, out_payload, *inout_len, used))
    {
        return false;
    }

    if (request_id_len != 0)
    {
        if (!appendBin(request_id, request_id_len, out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendNil(out_payload, *inout_len, used))
    {
        return false;
    }

    if (!appendBin(reinterpret_cast<const uint8_t*>("f"), 1, out_payload, *inout_len, used) ||
        !appendUint(flags, out_payload, *inout_len, used) ||
        !appendBin(reinterpret_cast<const uint8_t*>("m"), 1, out_payload, *inout_len, used) ||
        !appendBin(hashmap, hashmap_len, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodeResourceAdvertisement(const uint8_t* data, size_t len,
                                 DecodedResourceAdvertisement* out_advertisement)
{
    if (!data || len == 0 || !out_advertisement)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;
    size_t map_count = 0;
    if (!readMapHeader(cursor, &map_count))
    {
        return false;
    }

    DecodedResourceAdvertisement decoded{};
    bool have_hash = false;
    bool have_random = false;
    bool have_original = false;
    bool have_hashmap = false;

    for (size_t i = 0; i < map_count; ++i)
    {
        std::vector<uint8_t> key_bytes;
        if (!readBinary(cursor, &key_bytes) || key_bytes.empty())
        {
            return false;
        }

        const char key = static_cast<char>(key_bytes[0]);
        switch (key)
        {
        case 't':
        {
            uint32_t value = 0;
            if (!readUint(cursor, &value))
            {
                return false;
            }
            decoded.transfer_size = value;
            break;
        }
        case 'd':
        {
            uint32_t value = 0;
            if (!readUint(cursor, &value))
            {
                return false;
            }
            decoded.data_size = value;
            break;
        }
        case 'n':
        {
            uint32_t value = 0;
            if (!readUint(cursor, &value))
            {
                return false;
            }
            decoded.part_count = value;
            break;
        }
        case 'h':
        {
            std::vector<uint8_t> value;
            if (!readBinary(cursor, &value) || value.size() != reticulum::kFullHashSize)
            {
                return false;
            }
            memcpy(decoded.resource_hash, value.data(), value.size());
            have_hash = true;
            break;
        }
        case 'r':
        {
            std::vector<uint8_t> value;
            if (!readBinary(cursor, &value) || value.size() != sizeof(decoded.random_hash))
            {
                return false;
            }
            memcpy(decoded.random_hash, value.data(), value.size());
            have_random = true;
            break;
        }
        case 'o':
        {
            std::vector<uint8_t> value;
            if (!readBinary(cursor, &value) || value.size() != reticulum::kFullHashSize)
            {
                return false;
            }
            memcpy(decoded.original_hash, value.data(), value.size());
            have_original = true;
            break;
        }
        case 'i':
        {
            uint32_t value = 0;
            if (!readUint(cursor, &value))
            {
                return false;
            }
            decoded.segment_index = value;
            break;
        }
        case 'l':
        {
            uint32_t value = 0;
            if (!readUint(cursor, &value))
            {
                return false;
            }
            decoded.total_segments = value;
            break;
        }
        case 'q':
        {
            uint8_t next = 0;
            if (!peekByte(cursor, &next))
            {
                return false;
            }
            if (next == 0xC0)
            {
                if (!readNil(cursor))
                {
                    return false;
                }
            }
            else if (!readBinary(cursor, &decoded.request_id))
            {
                return false;
            }
            break;
        }
        case 'f':
        {
            uint32_t value = 0;
            if (!readUint(cursor, &value))
            {
                return false;
            }
            decoded.flags = static_cast<uint8_t>(value & 0xFFU);
            break;
        }
        case 'm':
        {
            if (!readBinary(cursor, &decoded.hashmap))
            {
                return false;
            }
            have_hashmap = true;
            break;
        }
        default:
            if (!skipObject(cursor))
            {
                return false;
            }
            break;
        }
    }

    if (!have_hash || !have_random || !have_original || !have_hashmap)
    {
        return false;
    }

    *out_advertisement = std::move(decoded);
    return true;
}

bool encodeResourceHashmapUpdate(uint32_t segment,
                                 const uint8_t* hashmap,
                                 size_t hashmap_len,
                                 uint8_t* out_payload,
                                 size_t* inout_len)
{
    if (!hashmap || hashmap_len == 0 || !out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(2, out_payload, *inout_len, used) ||
        !appendUint(segment, out_payload, *inout_len, used) ||
        !appendBin(hashmap, hashmap_len, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodeResourceHashmapUpdate(const uint8_t* data, size_t len,
                                 DecodedResourceHashmapUpdate* out_update)
{
    if (!data || len == 0 || !out_update)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;
    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count != 2)
    {
        return false;
    }

    DecodedResourceHashmapUpdate decoded{};
    if (!readUint(cursor, &decoded.segment) ||
        !readBinary(cursor, &decoded.hashmap))
    {
        return false;
    }

    *out_update = std::move(decoded);
    return true;
}

bool encodeMsgpackBool(bool value,
                       uint8_t* out_payload,
                       size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendBool(value, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool encodeMsgpackUint(uint32_t value,
                       uint8_t* out_payload,
                       size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendUint(value, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool encodePropagationBatch(double remote_timebase,
                            const std::vector<std::vector<uint8_t>>& messages,
                            uint8_t* out_payload,
                            size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(2, out_payload, *inout_len, used) ||
        !appendFloat64(remote_timebase, out_payload, *inout_len, used) ||
        !appendArrayOfBins(messages, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodePropagationBatch(const uint8_t* data, size_t len,
                            DecodedPropagationBatch* out_batch)
{
    if (!data || len == 0 || !out_batch)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;

    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count != 2)
    {
        return false;
    }

    DecodedPropagationBatch decoded{};
    if (!readFloat64(cursor, &decoded.remote_timebase) ||
        !readArrayOfBins(cursor, &decoded.messages))
    {
        return false;
    }

    *out_batch = std::move(decoded);
    return true;
}

bool decodePropagationOfferPayload(const uint8_t* data, size_t len,
                                   DecodedPropagationOffer* out_offer)
{
    if (!data || len == 0 || !out_offer)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;

    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count < 2)
    {
        return false;
    }

    DecodedPropagationOffer decoded{};
    uint8_t next = 0;
    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        if (!readNil(cursor))
        {
            return false;
        }
    }
    else
    {
        if (!readBinary(cursor, &decoded.peering_key))
        {
            return false;
        }
        decoded.peering_key_is_nil = false;
    }

    if (!readArrayOfBins(cursor, &decoded.transient_ids))
    {
        return false;
    }

    *out_offer = std::move(decoded);
    return true;
}

bool decodePropagationGetRequestPayload(const uint8_t* data, size_t len,
                                        DecodedPropagationGetRequest* out_request)
{
    if (!data || len == 0 || !out_request)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;

    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count < 2)
    {
        return false;
    }

    DecodedPropagationGetRequest decoded{};
    uint8_t next = 0;
    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        if (!readNil(cursor))
        {
            return false;
        }
    }
    else
    {
        if (!readArrayOfBins(cursor, &decoded.wants))
        {
            return false;
        }
        decoded.wants_is_nil = false;
    }

    if (!peekByte(cursor, &next))
    {
        return false;
    }
    if (next == 0xC0)
    {
        if (!readNil(cursor))
        {
            return false;
        }
    }
    else
    {
        if (!readArrayOfBins(cursor, &decoded.haves))
        {
            return false;
        }
        decoded.haves_is_nil = false;
    }

    if (count >= 3)
    {
        uint32_t limit_kb = 0;
        if (!readUint(cursor, &limit_kb))
        {
            return false;
        }
        decoded.has_transfer_limit = true;
        decoded.transfer_limit_kb = limit_kb;
    }

    *out_request = std::move(decoded);
    return true;
}

bool encodePropagationIdListPayload(const std::vector<std::vector<uint8_t>>& ids,
                                    uint8_t* out_payload,
                                    size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayOfBins(ids, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool encodePropagationMessageListPayload(const std::vector<std::vector<uint8_t>>& messages,
                                         uint8_t* out_payload,
                                         size_t* inout_len)
{
    return encodePropagationIdListPayload(messages, out_payload, inout_len);
}

void computeMessageHash(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                        const uint8_t source_hash[reticulum::kTruncatedHashSize],
                        const uint8_t* packed_payload,
                        size_t packed_payload_len,
                        uint8_t out_hash[reticulum::kFullHashSize])
{
    std::vector<uint8_t> material((reticulum::kTruncatedHashSize * 2) + packed_payload_len);
    size_t used = 0;
    memcpy(material.data() + used, destination_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(material.data() + used, source_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    if (packed_payload && packed_payload_len != 0)
    {
        memcpy(material.data() + used, packed_payload, packed_payload_len);
        used += packed_payload_len;
    }
    reticulum::fullHash(material.data(), used, out_hash);
}

bool buildSignedPart(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                     const uint8_t source_hash[reticulum::kTruncatedHashSize],
                     const uint8_t* packed_payload,
                     size_t packed_payload_len,
                     uint8_t* out_signed_part,
                     size_t* inout_len,
                     uint8_t out_message_hash[reticulum::kFullHashSize])
{
    if (!destination_hash || !source_hash || !out_signed_part || !inout_len || !out_message_hash)
    {
        return false;
    }

    computeMessageHash(destination_hash, source_hash, packed_payload, packed_payload_len, out_message_hash);

    const size_t total_len = (reticulum::kTruncatedHashSize * 2) +
                             packed_payload_len +
                             reticulum::kFullHashSize;
    if (*inout_len < total_len)
    {
        *inout_len = total_len;
        return false;
    }

    size_t used = 0;
    memcpy(out_signed_part + used, destination_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(out_signed_part + used, source_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    if (packed_payload && packed_payload_len != 0)
    {
        memcpy(out_signed_part + used, packed_payload, packed_payload_len);
        used += packed_payload_len;
    }
    memcpy(out_signed_part + used, out_message_hash, reticulum::kFullHashSize);
    used += reticulum::kFullHashSize;

    *inout_len = used;
    return true;
}

bool packMessage(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                 const uint8_t source_hash[reticulum::kTruncatedHashSize],
                 const uint8_t signature[reticulum::kSignatureSize],
                 const uint8_t* packed_payload,
                 size_t packed_payload_len,
                 uint8_t* out_message,
                 size_t* inout_len)
{
    if (!destination_hash || !source_hash || !signature || !out_message || !inout_len)
    {
        return false;
    }

    const size_t total_len = (reticulum::kTruncatedHashSize * 2) +
                             reticulum::kSignatureSize +
                             packed_payload_len;
    if (*inout_len < total_len)
    {
        *inout_len = total_len;
        return false;
    }

    size_t used = 0;
    memcpy(out_message + used, destination_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(out_message + used, source_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(out_message + used, signature, reticulum::kSignatureSize);
    used += reticulum::kSignatureSize;
    if (packed_payload && packed_payload_len != 0)
    {
        memcpy(out_message + used, packed_payload, packed_payload_len);
        used += packed_payload_len;
    }
    *inout_len = used;
    return true;
}

bool unpackMessageEnvelope(const uint8_t* data, size_t len, DecodedEnvelope* out_envelope)
{
    if (!data || len < ((reticulum::kTruncatedHashSize * 2) + reticulum::kSignatureSize) || !out_envelope)
    {
        return false;
    }

    DecodedEnvelope decoded{};
    memcpy(decoded.destination_hash, data, reticulum::kTruncatedHashSize);
    memcpy(decoded.source_hash, data + reticulum::kTruncatedHashSize, reticulum::kTruncatedHashSize);
    memcpy(decoded.signature,
           data + (reticulum::kTruncatedHashSize * 2),
           reticulum::kSignatureSize);

    const uint8_t* payload_ptr = data + (reticulum::kTruncatedHashSize * 2) + reticulum::kSignatureSize;
    const size_t payload_len = len - ((reticulum::kTruncatedHashSize * 2) + reticulum::kSignatureSize);
    decoded.packed_payload.assign(payload_ptr, payload_ptr + payload_len);
    *out_envelope = std::move(decoded);
    return true;
}

bool unpackTextPayload(const uint8_t* data, size_t len, DecodedTextPayload* out_payload)
{
    if (!data || len < 4 || !out_payload)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;

    size_t element_count = 0;
    if (!readArrayHeader(cursor, &element_count) || element_count < 4 || element_count > 5)
    {
        return false;
    }

    DecodedTextPayload decoded{};
    if (!readFloat64(cursor, &decoded.timestamp))
    {
        return false;
    }

    std::vector<uint8_t> title_bytes;
    std::vector<uint8_t> content_bytes;
    if (!readBinary(cursor, &title_bytes) || !readBinary(cursor, &content_bytes))
    {
        return false;
    }
    decoded.title.assign(title_bytes.begin(), title_bytes.end());
    decoded.content.assign(content_bytes.begin(), content_bytes.end());

    size_t map_count = 0;
    if (!readMapHeader(cursor, &map_count))
    {
        return false;
    }
    decoded.fields_empty = (map_count == 0);
    for (size_t i = 0; i < map_count; ++i)
    {
        if (!skipObject(cursor) || !skipObject(cursor))
        {
            return false;
        }
    }

    if (element_count == 5)
    {
        uint8_t next = 0;
        if (!peekByte(cursor, &next))
        {
            return false;
        }
        if (next == 0xC0)
        {
            if (!readNil(cursor))
            {
                return false;
            }
        }
        else
        {
            decoded.has_stamp = true;
            if (!readBinary(cursor, &decoded.stamp))
            {
                return false;
            }
        }
    }

    *out_payload = std::move(decoded);
    return true;
}

bool decodeAppDataPayload(const uint8_t* data, size_t len, DecodedAppData* out_payload)
{
    if (!data || len < kAppPayloadHeaderLen || !out_payload)
    {
        return false;
    }
    if (memcmp(data, kAppPayloadMagic, sizeof(kAppPayloadMagic)) != 0)
    {
        return false;
    }
    if (data[4] != kAppPayloadVersion)
    {
        return false;
    }

    DecodedAppData decoded{};
    decoded.version = data[4];
    decoded.want_response = (data[5] & kAppPayloadFlagWantResponse) != 0;
    decoded.portnum = readU32Be(data + 6);
    decoded.packet_id = readU32Be(data + 10);
    decoded.request_id = readU32Be(data + 14);
    decoded.payload.assign(data + kAppPayloadHeaderLen, data + len);
    *out_payload = std::move(decoded);
    return true;
}

bool unpackMessage(const uint8_t* data, size_t len, DecodedMessage* out_message)
{
    if (!out_message)
    {
        return false;
    }

    DecodedEnvelope envelope{};
    if (!unpackMessageEnvelope(data, len, &envelope))
    {
        return false;
    }

    DecodedTextPayload payload{};
    if (!unpackTextPayload(envelope.packed_payload.data(), envelope.packed_payload.size(), &payload))
    {
        return false;
    }

    DecodedMessage decoded{};
    memcpy(decoded.destination_hash, envelope.destination_hash, sizeof(decoded.destination_hash));
    memcpy(decoded.source_hash, envelope.source_hash, sizeof(decoded.source_hash));
    memcpy(decoded.signature, envelope.signature, sizeof(decoded.signature));
    decoded.packed_payload = std::move(envelope.packed_payload);
    decoded.timestamp = payload.timestamp;
    decoded.title = std::move(payload.title);
    decoded.content = std::move(payload.content);
    decoded.has_stamp = payload.has_stamp;
    decoded.stamp = std::move(payload.stamp);
    decoded.fields_empty = payload.fields_empty;
    *out_message = std::move(decoded);
    return true;
}

} // namespace chat::lxmf
