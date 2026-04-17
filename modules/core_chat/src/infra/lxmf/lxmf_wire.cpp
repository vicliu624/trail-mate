/**
 * @file lxmf_wire.cpp
 * @brief Shared LXMF wire helpers for direct text-message subsets
 */

#include "chat/infra/lxmf/lxmf_wire.h"

#include <algorithm>
#include <cstring>

namespace chat::lxmf
{
namespace
{
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
    return appendByte(static_cast<uint8_t>(0x90U | (count & 0x0FU)), out, out_len, used);
}

bool appendMapHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    return appendByte(static_cast<uint8_t>(0x80U | (count & 0x0FU)), out, out_len, used);
}

bool appendNil(uint8_t* out, size_t out_len, size_t& used)
{
    return appendByte(0xC0, out, out_len, used);
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
    return false;
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
    if (!readByte(cursor, &tag) || (tag & 0xF0U) != 0x90U || !out_count)
    {
        return false;
    }
    *out_count = static_cast<size_t>(tag & 0x0FU);
    return true;
}

bool readMapHeader(Cursor& cursor, size_t* out_count)
{
    uint8_t tag = 0;
    if (!readByte(cursor, &tag) || (tag & 0xF0U) != 0x80U || !out_count)
    {
        return false;
    }
    *out_count = static_cast<size_t>(tag & 0x0FU);
    return true;
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
    if (tag == 0xCB)
    {
        double ignored = 0.0;
        return readFloat64(cursor, &ignored);
    }
    if (tag == 0xCC || tag <= 0x7F)
    {
        uint32_t ignored = 0;
        return readUint(cursor, &ignored);
    }
    if ((tag & 0xF0U) == 0x80U)
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
    if ((tag & 0xF0U) == 0x90U)
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

void computeMessageHash(const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                        const uint8_t source_hash[reticulum::kTruncatedHashSize],
                        const uint8_t* packed_payload,
                        size_t packed_payload_len,
                        uint8_t out_hash[reticulum::kFullHashSize])
{
    uint8_t material[reticulum::kReticulumMtu] = {};
    size_t used = 0;
    memcpy(material + used, destination_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(material + used, source_hash, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    if (packed_payload && packed_payload_len != 0)
    {
        memcpy(material + used, packed_payload, packed_payload_len);
        used += packed_payload_len;
    }
    reticulum::fullHash(material, used, out_hash);
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

bool unpackMessage(const uint8_t* data, size_t len, DecodedMessage* out_message)
{
    if (!data || len < ((reticulum::kTruncatedHashSize * 2) + reticulum::kSignatureSize + 4) || !out_message)
    {
        return false;
    }

    DecodedMessage decoded{};
    memcpy(decoded.destination_hash, data, reticulum::kTruncatedHashSize);
    memcpy(decoded.source_hash, data + reticulum::kTruncatedHashSize, reticulum::kTruncatedHashSize);
    memcpy(decoded.signature,
           data + (reticulum::kTruncatedHashSize * 2),
           reticulum::kSignatureSize);

    const uint8_t* payload_ptr = data + (reticulum::kTruncatedHashSize * 2) + reticulum::kSignatureSize;
    const size_t payload_len = len - ((reticulum::kTruncatedHashSize * 2) + reticulum::kSignatureSize);
    Cursor cursor;
    cursor.data = payload_ptr;
    cursor.len = payload_len;
    cursor.pos = 0;

    size_t element_count = 0;
    if (!readArrayHeader(cursor, &element_count) || element_count < 4 || element_count > 5)
    {
        return false;
    }

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

    decoded.packed_payload.assign(payload_ptr, payload_ptr + cursor.pos);
    *out_message = std::move(decoded);
    return true;
}

} // namespace chat::lxmf
