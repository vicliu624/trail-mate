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
constexpr uint32_t kSidebandSensorLocation = 0x02;
constexpr uint32_t kSidebandCommandTelemetryRequest = 0x01;
constexpr size_t kMaxPropagationWireItems = 64;

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

bool appendString(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if (len <= 0x1FU)
    {
        return appendByte(static_cast<uint8_t>(0xA0U | (len & 0x1FU)), out, out_len, used) &&
               appendBytes(data, len, out, out_len, used);
    }
    if (len <= 0xFFU)
    {
        return appendByte(0xD9, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(len), out, out_len, used) &&
               appendBytes(data, len, out, out_len, used);
    }
    if (len <= 0xFFFFU)
    {
        return appendByte(0xDA, out, out_len, used) &&
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

bool readNonNegativeNumber(Cursor& cursor, uint32_t* out_value)
{
    if (!out_value)
    {
        return false;
    }

    uint8_t tag = 0;
    if (!peekByte(cursor, &tag))
    {
        return false;
    }
    if (tag == 0xCB)
    {
        double value = 0.0;
        if (!readFloat64(cursor, &value) || value < 0.0 || value > 4294967295.0)
        {
            return false;
        }
        *out_value = static_cast<uint32_t>(value);
        return true;
    }
    return readUint(cursor, out_value);
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

bool readBinarySpan(Cursor& cursor, const uint8_t** out_data, size_t* out_len)
{
    if (!out_data || !out_len)
    {
        return false;
    }
    *out_data = nullptr;
    *out_len = 0;

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
    else if (tag == 0xDA)
    {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!readByte(cursor, &hi) || !readByte(cursor, &lo))
        {
            return false;
        }
        len = static_cast<size_t>((static_cast<uint16_t>(hi) << 8) | lo);
    }
    else
    {
        return false;
    }

    if (cursor.pos + len > cursor.len)
    {
        return false;
    }

    *out_data = cursor.data + cursor.pos;
    *out_len = len;
    cursor.pos += len;
    return true;
}

bool readBinary(Cursor& cursor, std::vector<uint8_t>* out_data)
{
    if (!out_data)
    {
        return false;
    }

    const uint8_t* data = nullptr;
    size_t len = 0;
    if (!readBinarySpan(cursor, &data, &len))
    {
        return false;
    }

    out_data->assign(data, data + len);
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
    if (items.size() > kMaxPropagationWireItems)
    {
        return false;
    }
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

bool appendArrayOfBinSpans(ByteSpanList items,
                           uint8_t* out,
                           size_t out_len,
                           size_t& used);

bool appendArrayOfBinSpans(const std::vector<ByteSpan>& items,
                           uint8_t* out,
                           size_t out_len,
                           size_t& used)
{
    return appendArrayOfBinSpans(ByteSpanList{items.data(), items.size()},
                                 out,
                                 out_len,
                                 used);
}

bool appendArrayOfBinSpans(ByteSpanList items,
                           uint8_t* out,
                           size_t out_len,
                           size_t& used)
{
    if (items.size > kMaxPropagationWireItems)
    {
        return false;
    }
    if (items.size != 0U && !items.items)
    {
        return false;
    }
    if (!appendArrayHeader(static_cast<uint8_t>(items.size), out, out_len, used))
    {
        return false;
    }

    for (size_t index = 0; index < items.size; ++index)
    {
        const ByteSpan& item = items.items[index];
        if ((!item.data && item.size != 0U) ||
            !appendBin(item.data, item.size, out, out_len, used))
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
    if (count > kMaxPropagationWireItems ||
        count > cursor.len - cursor.pos)
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

bool readArrayOfBins(Cursor& cursor,
                     BinItemCallback on_item,
                     void* callback_context)
{
    if (!on_item)
    {
        return false;
    }

    size_t count = 0;
    if (!readArrayHeader(cursor, &count))
    {
        return false;
    }
    if (count > kMaxPropagationWireItems ||
        count > cursor.len - cursor.pos)
    {
        return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t* item_data = nullptr;
        size_t item_len = 0;
        if (!readBinarySpan(cursor, &item_data, &item_len) ||
            !on_item(item_data, item_len, callback_context))
        {
            return false;
        }
    }

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

void copyAnnounceDisplayName(const std::vector<uint8_t>& name,
                             char* out_display_name,
                             size_t display_name_len)
{
    if (!out_display_name || display_name_len == 0)
    {
        return;
    }
    const size_t copy_len = std::min(name.size(), display_name_len - 1);
    for (size_t i = 0; i < copy_len; ++i)
    {
        const uint8_t byte = name[i];
        out_display_name[i] =
            (byte == '\t' || byte == '\r' || byte == '\n') ? ' ' : static_cast<char>(byte);
    }
    out_display_name[copy_len] = '\0';
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
    if (!readArrayHeader(cursor, &count))
    {
        std::vector<uint8_t> legacy_name(data, data + len);
        copyAnnounceDisplayName(legacy_name, out_display_name, display_name_len);
        return out_display_name[0] != '\0';
    }
    if (count < 1)
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
        copyAnnounceDisplayName(name, out_display_name, display_name_len);
    }

    if (count == 1)
    {
        return true;
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
    }

    for (size_t index = 2; index < count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
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

bool encodeCustomDataPayload(double timestamp, const char* title, const char* content,
                             const char* custom_type, ByteSpan custom_data,
                             uint8_t* out_payload, size_t* inout_len)
{
    if (!inout_len) return false;
    const size_t capacity = *inout_len;
    *inout_len = 0;
    if (!out_payload || !custom_type || !custom_type[0] ||
        (!custom_data.data && custom_data.size) || custom_data.size > 65535) return false;
    const auto* title_bytes = reinterpret_cast<const uint8_t*>(title ? title : "");
    const auto* content_bytes = reinterpret_cast<const uint8_t*>(content ? content : "");
    const auto* type_bytes = reinterpret_cast<const uint8_t*>(custom_type);
    const size_t title_len = title ? strlen(title) : 0;
    const size_t content_len = content ? strlen(content) : 0;
    const size_t type_len = strlen(custom_type);
    if (title_len > 65535 || content_len > 65535 || type_len > 65535) return false;
    size_t used = 0;
    if (!appendArrayHeader(4, out_payload, capacity, used) ||
        !appendFloat64(timestamp, out_payload, capacity, used) ||
        !appendBin(title_bytes, title_len, out_payload, capacity, used) ||
        !appendBin(content_bytes, content_len, out_payload, capacity, used) ||
        !appendMapHeader(2, out_payload, capacity, used) ||
        !appendUint(0xfb, out_payload, capacity, used) ||
        !appendString(type_bytes, type_len, out_payload, capacity, used) ||
        !appendUint(0xfc, out_payload, capacity, used) ||
        !appendBin(custom_data.data, custom_data.size, out_payload, capacity, used)) return false;
    *inout_len = used;
    return true;
}

bool encodeSidebandTelemetryLocationPayload(
    double message_timestamp,
    const SidebandTelemetryLocation& location,
    uint8_t* out_payload,
    size_t* inout_len)
{
    if (!out_payload || !inout_len ||
        location.latitude_e6 < -90000000 ||
        location.latitude_e6 > 90000000 ||
        location.longitude_e6 < -180000000 ||
        location.longitude_e6 > 180000000 ||
        location.accuracy_cm > 0xFFFFU)
    {
        return false;
    }

    uint8_t packed_telemetry[48] = {};
    size_t telemetry_used = 0;
    uint8_t latitude[4] = {};
    uint8_t longitude[4] = {};
    uint8_t altitude[4] = {};
    const uint8_t zero_u32[4] = {};
    uint8_t accuracy[2] = {};
    writeU32Be(static_cast<uint32_t>(location.latitude_e6), latitude);
    writeU32Be(static_cast<uint32_t>(location.longitude_e6), longitude);
    writeU32Be(static_cast<uint32_t>(location.altitude_cm), altitude);
    accuracy[0] = static_cast<uint8_t>((location.accuracy_cm >> 8U) & 0xFFU);
    accuracy[1] = static_cast<uint8_t>(location.accuracy_cm & 0xFFU);

    if (!appendMapHeader(1,
                         packed_telemetry,
                         sizeof(packed_telemetry),
                         telemetry_used) ||
        !appendUint(kSidebandSensorLocation,
                    packed_telemetry,
                    sizeof(packed_telemetry),
                    telemetry_used) ||
        !appendArrayHeader(7,
                           packed_telemetry,
                           sizeof(packed_telemetry),
                           telemetry_used) ||
        !appendBin(latitude,
                   sizeof(latitude),
                   packed_telemetry,
                   sizeof(packed_telemetry),
                   telemetry_used) ||
        !appendBin(longitude,
                   sizeof(longitude),
                   packed_telemetry,
                   sizeof(packed_telemetry),
                   telemetry_used) ||
        !appendBin(altitude,
                   sizeof(altitude),
                   packed_telemetry,
                   sizeof(packed_telemetry),
                   telemetry_used) ||
        !appendBin(zero_u32,
                   sizeof(zero_u32),
                   packed_telemetry,
                   sizeof(packed_telemetry),
                   telemetry_used) ||
        !appendBin(zero_u32,
                   sizeof(zero_u32),
                   packed_telemetry,
                   sizeof(packed_telemetry),
                   telemetry_used) ||
        !appendBin(accuracy,
                   sizeof(accuracy),
                   packed_telemetry,
                   sizeof(packed_telemetry),
                   telemetry_used) ||
        !appendUint(location.timestamp,
                    packed_telemetry,
                    sizeof(packed_telemetry),
                    telemetry_used))
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(4, out_payload, *inout_len, used) ||
        !appendFloat64(message_timestamp, out_payload, *inout_len, used) ||
        !appendBin(nullptr, 0, out_payload, *inout_len, used) ||
        !appendBin(nullptr, 0, out_payload, *inout_len, used) ||
        !appendMapHeader(1, out_payload, *inout_len, used) ||
        !appendUint(kFieldTelemetry, out_payload, *inout_len, used) ||
        !appendBin(packed_telemetry,
                   telemetry_used,
                   out_payload,
                   *inout_len,
                   used))
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
    if (!appendMapHeader(11, out_payload, *inout_len, used) ||
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

bool encodePropagationBatch(double remote_timebase,
                            const std::vector<ByteSpan>& messages,
                            uint8_t* out_payload,
                            size_t* inout_len)
{
    return encodePropagationBatch(remote_timebase,
                                  ByteSpanList{messages.data(), messages.size()},
                                  out_payload,
                                  inout_len);
}

bool encodePropagationBatch(double remote_timebase,
                            ByteSpanList messages,
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
        !appendArrayOfBinSpans(messages, out_payload, *inout_len, used))
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
        !readArrayOfBins(cursor, &decoded.messages) ||
        cursor.pos != cursor.len)
    {
        return false;
    }

    *out_batch = std::move(decoded);
    return true;
}

bool decodePropagationBatch(const uint8_t* data,
                            size_t len,
                            BinItemCallback on_message,
                            void* callback_context,
                            double* out_remote_timebase)
{
    if (!data || len == 0 || !on_message || !out_remote_timebase)
    {
        return false;
    }

    Cursor cursor;
    cursor.data = data;
    cursor.len = len;
    cursor.pos = 0;

    size_t count = 0;
    double remote_timebase = 0.0;
    if (!readArrayHeader(cursor, &count) || count != 2 ||
        !readFloat64(cursor, &remote_timebase) ||
        !readArrayOfBins(cursor, on_message, callback_context) ||
        cursor.pos != cursor.len)
    {
        return false;
    }

    *out_remote_timebase = remote_timebase;
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
    for (size_t index = 2; index < count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
    }
    if (cursor.pos != cursor.len)
    {
        return false;
    }

    *out_offer = std::move(decoded);
    return true;
}

bool decodePropagationOfferPayload(const uint8_t* data,
                                   size_t len,
                                   BinItemCallback on_transient_id,
                                   void* callback_context,
                                   DecodedPropagationOfferHeader* out_offer)
{
    if (!data || len == 0 || !on_transient_id || !out_offer)
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

    DecodedPropagationOfferHeader decoded{};
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
        const uint8_t* peering_key = nullptr;
        size_t peering_key_len = 0;
        if (!readBinarySpan(cursor, &peering_key, &peering_key_len))
        {
            return false;
        }
        decoded.peering_key_is_nil = false;
        decoded.peering_key = ByteSpan{peering_key, peering_key_len};
    }

    if (!readArrayOfBins(cursor, on_transient_id, callback_context))
    {
        return false;
    }
    for (size_t index = 2; index < count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
    }
    if (cursor.pos != cursor.len)
    {
        return false;
    }

    *out_offer = decoded;
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
    for (size_t index = 3; index < count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
    }
    if (cursor.pos != cursor.len)
    {
        return false;
    }

    *out_request = std::move(decoded);
    return true;
}

bool decodePropagationGetRequestPayload(
    const uint8_t* data,
    size_t len,
    BinItemCallback on_want,
    void* want_context,
    BinItemCallback on_have,
    void* have_context,
    DecodedPropagationGetRequestHeader* out_request)
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

    DecodedPropagationGetRequestHeader decoded{};
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
        if (!on_want ||
            !readArrayOfBins(cursor, on_want, want_context))
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
        if (!on_have ||
            !readArrayOfBins(cursor, on_have, have_context))
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
    for (size_t index = 3; index < count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
    }
    if (cursor.pos != cursor.len)
    {
        return false;
    }

    *out_request = decoded;
    return true;
}

bool encodePropagationGetRequestPayload(
    const std::vector<std::vector<uint8_t>>* wants,
    const std::vector<std::vector<uint8_t>>* haves,
    bool include_transfer_limit,
    uint32_t transfer_limit_kb,
    uint8_t* out_payload,
    size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(include_transfer_limit ? 3 : 2,
                           out_payload,
                           *inout_len,
                           used))
    {
        return false;
    }
    if (wants)
    {
        if (!appendArrayOfBins(*wants, out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendNil(out_payload, *inout_len, used))
    {
        return false;
    }
    if (haves)
    {
        if (!appendArrayOfBins(*haves, out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendNil(out_payload, *inout_len, used))
    {
        return false;
    }
    if (include_transfer_limit &&
        !appendUint(transfer_limit_kb, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool encodePropagationGetRequestPayloadSpans(const ByteSpanList* wants,
                                             const ByteSpanList* haves,
                                             bool include_transfer_limit,
                                             uint32_t transfer_limit_kb,
                                             uint8_t* out_payload,
                                             size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayHeader(include_transfer_limit ? 3 : 2,
                           out_payload,
                           *inout_len,
                           used))
    {
        return false;
    }
    if (wants)
    {
        if (!appendArrayOfBinSpans(*wants, out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendNil(out_payload, *inout_len, used))
    {
        return false;
    }
    if (haves)
    {
        if (!appendArrayOfBinSpans(*haves, out_payload, *inout_len, used))
        {
            return false;
        }
    }
    else if (!appendNil(out_payload, *inout_len, used))
    {
        return false;
    }
    if (include_transfer_limit &&
        !appendUint(transfer_limit_kb, out_payload, *inout_len, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodePropagationIdListPayload(
    const uint8_t* data,
    size_t len,
    std::vector<std::vector<uint8_t>>* out_ids)
{
    if (!data || len == 0 || !out_ids)
    {
        return false;
    }
    Cursor cursor{data, len, 0};
    std::vector<std::vector<uint8_t>> ids;
    if (!readArrayOfBins(cursor, &ids) || cursor.pos != cursor.len)
    {
        return false;
    }
    *out_ids = std::move(ids);
    return true;
}

bool decodePropagationIdListPayload(const uint8_t* data,
                                    size_t len,
                                    BinItemCallback on_item,
                                    void* callback_context)
{
    if (!data || len == 0 || !on_item)
    {
        return false;
    }
    Cursor cursor{data, len, 0};
    if (!readArrayOfBins(cursor, on_item, callback_context) ||
        cursor.pos != cursor.len)
    {
        return false;
    }
    return true;
}

bool decodePropagationMessageListPayload(
    const uint8_t* data,
    size_t len,
    std::vector<std::vector<uint8_t>>* out_messages)
{
    return decodePropagationIdListPayload(data, len, out_messages);
}

bool decodePropagationMessageListPayload(const uint8_t* data,
                                         size_t len,
                                         BinItemCallback on_message,
                                         void* callback_context)
{
    return decodePropagationIdListPayload(data, len, on_message, callback_context);
}

bool decodePropagationAnnounceAppData(
    const uint8_t* data,
    size_t len,
    DecodedPropagationAnnounce* out_announce)
{
    if (!data || len == 0 || !out_announce)
    {
        return false;
    }

    Cursor cursor{data, len, 0};
    size_t count = 0;
    if (!readArrayHeader(cursor, &count) || count < 7)
    {
        return false;
    }

    DecodedPropagationAnnounce decoded{};
    uint32_t stamp_cost = 0;
    uint32_t stamp_flexibility = 0;
    uint32_t peering_cost = 0;
    size_t stamp_count = 0;
    if (!readBool(cursor, &decoded.legacy_support) ||
        !readNonNegativeNumber(cursor, &decoded.timebase_s) ||
        !readBool(cursor, &decoded.node_active) ||
        !readNonNegativeNumber(cursor, &decoded.transfer_limit_kb) ||
        !readNonNegativeNumber(cursor, &decoded.sync_limit_kb) ||
        !readArrayHeader(cursor, &stamp_count) || stamp_count < 3 ||
        !readUint(cursor, &stamp_cost) ||
        !readUint(cursor, &stamp_flexibility) ||
        !readUint(cursor, &peering_cost))
    {
        return false;
    }
    for (size_t index = 3; index < stamp_count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
    }

    size_t metadata_count = 0;
    if (!readMapHeader(cursor, &metadata_count))
    {
        return false;
    }
    for (size_t index = 0; index < metadata_count; ++index)
    {
        uint32_t key = 0;
        if (!readUint(cursor, &key))
        {
            return false;
        }
        if (key == 0x01U)
        {
            std::vector<uint8_t> name;
            if (!readBinary(cursor, &name))
            {
                return false;
            }
            decoded.display_name.assign(
                reinterpret_cast<const char*>(name.data()),
                name.size());
        }
        else if (!skipObject(cursor))
        {
            return false;
        }
    }
    for (size_t index = 7; index < count; ++index)
    {
        if (!skipObject(cursor))
        {
            return false;
        }
    }

    decoded.stamp_cost = static_cast<uint8_t>(std::min<uint32_t>(stamp_cost, 255U));
    decoded.stamp_cost_flexibility =
        static_cast<uint8_t>(std::min<uint32_t>(stamp_flexibility, 255U));
    decoded.peering_cost =
        static_cast<uint8_t>(std::min<uint32_t>(peering_cost, 255U));
    decoded.valid = decoded.node_active && cursor.pos == cursor.len;
    *out_announce = std::move(decoded);
    return cursor.pos == cursor.len;
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

bool encodePropagationIdListPayload(ByteSpanList ids,
                                    uint8_t* out_payload,
                                    size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayOfBinSpans(ids, out_payload, *inout_len, used))
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

bool encodePropagationMessageListPayload(const std::vector<ByteSpan>& messages,
                                         uint8_t* out_payload,
                                         size_t* inout_len)
{
    return encodePropagationMessageListPayload(
        ByteSpanList{messages.data(), messages.size()},
        out_payload,
        inout_len);
}

bool encodePropagationMessageListPayload(ByteSpanList messages,
                                         uint8_t* out_payload,
                                         size_t* inout_len)
{
    if (!out_payload || !inout_len)
    {
        return false;
    }

    size_t used = 0;
    if (!appendArrayOfBinSpans(messages, out_payload, *inout_len, used))
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
        Cursor key_cursor = cursor;
        uint32_t field_key = 0;
        const bool numeric_key = readUint(key_cursor, &field_key);
        if (numeric_key)
        {
            cursor = key_cursor;
        }
        else if (!skipObject(cursor))
        {
            return false;
        }

        const size_t value_start = cursor.pos;
        if (!skipObject(cursor))
        {
            return false;
        }
        if (numeric_key &&
            (field_key == kFieldTelemetry ||
             field_key == kFieldTelemetryStream ||
             field_key == kFieldCommands ||
             field_key == kFieldCustomType ||
             field_key == kFieldCustomData))
        {
            if ((field_key == kFieldCustomType && cursor.pos - value_start > 258) ||
                (field_key == kFieldCustomData && cursor.pos - value_start > 8195))
            {
                return false;
            }
            DecodedField field{};
            field.key = field_key;
            field.encoded_value.assign(data + value_start, data + cursor.pos);
            decoded.fields.push_back(std::move(field));
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

CustomDataResult extractCustomData(const DecodedTextPayload& payload,
                                   ByteSpan* out_type, ByteSpan* out_data)
{
    if (!out_type || !out_data) return CustomDataResult::Invalid;
    *out_type = {};
    *out_data = {};
    const DecodedField* type = nullptr;
    const DecodedField* data = nullptr;
    for (const auto& field : payload.fields)
    {
        if (field.key == kFieldCustomType)
        {
            if (type) return CustomDataResult::Invalid;
            type = &field;
        }
        if (field.key == kFieldCustomData)
        {
            if (data) return CustomDataResult::Invalid;
            data = &field;
        }
    }
    if (!type && !data) return CustomDataResult::NotCustom;
    if (!type || !data || type->encoded_value.empty() || data->encoded_value.empty()) return CustomDataResult::Invalid;
    const uint8_t type_tag = type->encoded_value[0];
    const uint8_t data_tag = data->encoded_value[0];
    if (!((type_tag & 0xe0) == 0xa0 || type_tag == 0xd9 || type_tag == 0xda) ||
        !(data_tag == 0xc4 || data_tag == 0xc5)) return CustomDataResult::Invalid;
    Cursor tc{type->encoded_value.data(), type->encoded_value.size(), 0};
    Cursor dc{data->encoded_value.data(), data->encoded_value.size(), 0};
    ByteSpan tv, dv;
    if (!readBinarySpan(tc, &tv.data, &tv.size) || tc.pos != tc.len || tv.size == 0 || tv.size > 96 ||
        !readBinarySpan(dc, &dv.data, &dv.size) || dc.pos != dc.len || dv.size > 8192) return CustomDataResult::Invalid;
    *out_type = tv;
    *out_data = dv;
    return CustomDataResult::Valid;
}

const DecodedField* findField(const DecodedTextPayload& payload, uint32_t key)
{
    const auto found = std::find_if(payload.fields.begin(),
                                    payload.fields.end(),
                                    [key](const DecodedField& field)
                                    { return field.key == key; });
    return found != payload.fields.end() ? &*found : nullptr;
}

bool decodeSidebandTelemetryLocation(const DecodedTextPayload& payload,
                                     SidebandTelemetryLocation* out_location)
{
    if (!out_location)
    {
        return false;
    }
    *out_location = SidebandTelemetryLocation{};

    const DecodedField* telemetry = findField(payload, kFieldTelemetry);
    if (!telemetry || telemetry->encoded_value.empty())
    {
        return false;
    }

    Cursor field_cursor{};
    field_cursor.data = telemetry->encoded_value.data();
    field_cursor.len = telemetry->encoded_value.size();
    std::vector<uint8_t> packed_telemetry;
    if (!readBinary(field_cursor, &packed_telemetry) || packed_telemetry.empty())
    {
        return false;
    }

    Cursor telemetry_cursor{};
    telemetry_cursor.data = packed_telemetry.data();
    telemetry_cursor.len = packed_telemetry.size();
    size_t sensor_count = 0;
    if (!readMapHeader(telemetry_cursor, &sensor_count))
    {
        return false;
    }

    for (size_t sensor_index = 0; sensor_index < sensor_count; ++sensor_index)
    {
        Cursor key_cursor = telemetry_cursor;
        uint32_t sensor_id = 0;
        const bool numeric_key = readUint(key_cursor, &sensor_id);
        if (numeric_key)
        {
            telemetry_cursor = key_cursor;
        }
        else
        {
            if (!skipObject(telemetry_cursor) || !skipObject(telemetry_cursor))
            {
                return false;
            }
            continue;
        }

        if (sensor_id != kSidebandSensorLocation)
        {
            if (!skipObject(telemetry_cursor))
            {
                return false;
            }
            continue;
        }

        size_t location_elements = 0;
        if (!readArrayHeader(telemetry_cursor, &location_elements) ||
            location_elements < 7)
        {
            return false;
        }

        std::vector<uint8_t> latitude;
        std::vector<uint8_t> longitude;
        std::vector<uint8_t> altitude;
        std::vector<uint8_t> speed;
        std::vector<uint8_t> bearing;
        std::vector<uint8_t> accuracy;
        uint32_t timestamp = 0;
        if (!readBinary(telemetry_cursor, &latitude) || latitude.size() != 4 ||
            !readBinary(telemetry_cursor, &longitude) || longitude.size() != 4 ||
            !readBinary(telemetry_cursor, &altitude) || altitude.size() != 4 ||
            !readBinary(telemetry_cursor, &speed) || speed.size() != 4 ||
            !readBinary(telemetry_cursor, &bearing) || bearing.size() != 4 ||
            !readBinary(telemetry_cursor, &accuracy) || accuracy.size() != 2 ||
            !readUint(telemetry_cursor, &timestamp))
        {
            return false;
        }
        for (size_t index = 7; index < location_elements; ++index)
        {
            if (!skipObject(telemetry_cursor))
            {
                return false;
            }
        }

        SidebandTelemetryLocation decoded{};
        decoded.latitude_e6 = static_cast<int32_t>(readU32Be(latitude.data()));
        decoded.longitude_e6 = static_cast<int32_t>(readU32Be(longitude.data()));
        decoded.altitude_cm = static_cast<int32_t>(readU32Be(altitude.data()));
        decoded.accuracy_cm =
            (static_cast<uint32_t>(accuracy[0]) << 8) | accuracy[1];
        decoded.timestamp = timestamp;
        decoded.valid = decoded.latitude_e6 >= -90000000 &&
                        decoded.latitude_e6 <= 90000000 &&
                        decoded.longitude_e6 >= -180000000 &&
                        decoded.longitude_e6 <= 180000000;
        if (!decoded.valid)
        {
            return false;
        }
        *out_location = decoded;
        return true;
    }

    return false;
}

bool decodeSidebandTelemetryRequest(const DecodedTextPayload& payload,
                                    SidebandTelemetryRequest* out_request)
{
    if (!out_request)
    {
        return false;
    }
    *out_request = SidebandTelemetryRequest{};

    const DecodedField* commands = findField(payload, kFieldCommands);
    if (!commands || commands->encoded_value.empty())
    {
        return false;
    }

    Cursor cursor{};
    cursor.data = commands->encoded_value.data();
    cursor.len = commands->encoded_value.size();
    size_t command_count = 0;
    if (!readArrayHeader(cursor, &command_count))
    {
        return false;
    }

    for (size_t command_index = 0; command_index < command_count; ++command_index)
    {
        size_t entry_count = 0;
        if (!readMapHeader(cursor, &entry_count))
        {
            return false;
        }
        for (size_t entry_index = 0; entry_index < entry_count; ++entry_index)
        {
            uint32_t command = 0;
            if (!readUint(cursor, &command))
            {
                return false;
            }
            if (command != kSidebandCommandTelemetryRequest)
            {
                if (!skipObject(cursor))
                {
                    return false;
                }
                continue;
            }

            SidebandTelemetryRequest decoded{};
            uint8_t next = 0;
            if (!peekByte(cursor, &next))
            {
                return false;
            }
            if ((next & 0xF0U) == 0x90U || next == 0xDC || next == 0xDD)
            {
                size_t request_elements = 0;
                if (!readArrayHeader(cursor, &request_elements) ||
                    request_elements < 1 ||
                    !readUint(cursor, &decoded.timebase))
                {
                    return false;
                }
                if (request_elements >= 2 &&
                    !readBool(cursor, &decoded.collector_request))
                {
                    return false;
                }
                for (size_t index = 2; index < request_elements; ++index)
                {
                    if (!skipObject(cursor))
                    {
                        return false;
                    }
                }
            }
            else if (!readUint(cursor, &decoded.timebase))
            {
                return false;
            }

            decoded.valid = true;
            *out_request = decoded;
            return true;
        }
    }

    return false;
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
    decoded.fields = std::move(payload.fields);
    *out_message = std::move(decoded);
    return true;
}

} // namespace chat::lxmf
