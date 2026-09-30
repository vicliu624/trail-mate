#include "chat/infra/reticulum/interface_discovery.h"

#include <cstring>

namespace chat::reticulum
{
namespace
{
enum class Kind : uint8_t
{
    Integer,
    String,
    Binary,
    Boolean,
    Nil,
    Float
};
struct Value
{
    Kind kind = Kind::Nil;
    const uint8_t* bytes = nullptr;
    uint32_t size = 0;
    uint32_t number = 0;
};

struct Reader
{
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    bool integer(size_t bytes, uint32_t& value)
    {
        if (bytes > size - pos) return false;
        value = 0;
        while (bytes--) value = (value << 8) | data[pos++];
        return true;
    }

    bool scalar(Value& value)
    {
        if (pos == size) return false;
        value = {};
        const uint8_t tag = data[pos++];
        if (tag <= 0x7f)
        {
            value.kind = Kind::Integer;
            value.number = tag;
            return true;
        }
        if (tag >= 0xa0 && tag <= 0xbf)
        {
            value.kind = Kind::String;
            value.size = tag & 0x1f;
        }
        else if (tag == 0xc0) return true;
        else if (tag == 0xc2 || tag == 0xc3)
        {
            value.kind = Kind::Boolean;
            value.number = tag == 0xc3;
            return true;
        }
        else if (tag >= 0xcc && tag <= 0xce)
        {
            value.kind = Kind::Integer;
            return integer(size_t{1} << (tag - 0xcc), value.number);
        }
        else if (tag >= 0xd9 && tag <= 0xdb)
        {
            value.kind = Kind::String;
            if (!integer(size_t{1} << (tag - 0xd9), value.size)) return false;
        }
        else if (tag >= 0xc4 && tag <= 0xc6)
        {
            value.kind = Kind::Binary;
            if (!integer(size_t{1} << (tag - 0xc4), value.size)) return false;
        }
        else if (tag == 0xca || tag == 0xcb)
        {
            value.kind = Kind::Float;
            value.size = tag == 0xca ? 4 : 8;
        }
        else return false; // Compound/extension values are outside this subset.
        if (value.size > size - pos) return false;
        value.bytes = data + pos;
        pos += value.size;
        return true;
    }
};

bool equals(const Value& value, const char* text)
{
    return value.kind == Kind::String && value.size == std::strlen(text) &&
           std::memcmp(value.bytes, text, value.size) == 0;
}

bool hostname(const Value& value)
{
    // Match the device's existing 63-character endpoint capacity. IPv6 requires
    // connector support first; IPv4 literals and ASCII DNS names are accepted.
    if (value.kind != Kind::String || value.size == 0 || value.size > 63) return false;
    bool label_start = true;
    uint8_t previous = 0;
    for (uint32_t i = 0; i < value.size; ++i)
    {
        const uint8_t c = value.bytes[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (c == '.')
        {
            if (label_start || previous == '-') return false;
            label_start = true;
        }
        else
        {
            if (!alnum && (c != '-' || label_start)) return false;
            label_start = false;
        }
        previous = c;
    }
    return !label_start && previous != '-';
}
} // namespace

bool parseInterfaceDiscovery(const uint8_t* app_data, size_t size, InterfaceDiscoveryView& out)
{
    out = {};
    // A complete Reticulum packet is at most 500 bytes. Reserve the final 32
    // bytes for the stamp and reject unsupported flags instead of guessing.
    if (!app_data || size <= 33 || size > 500 || app_data[0] != 0) return false;
    Reader reader{app_data + 1, size - 33};
    uint32_t count = 0;
    const uint8_t tag = reader.data[reader.pos++];
    if (tag >= 0x80 && tag <= 0x8f) count = tag & 0x0f;
    else if (tag == 0xde)
    {
        if (!reader.integer(2, count)) return false;
    }
    else return false;
    if (count == 0 || count > 32) return false;
    uint32_t seen[8] = {};
    InterfaceDiscoveryView result{};
    bool type_ok = false, transport = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        Value key, value;
        if (!reader.scalar(key) || key.kind != Kind::Integer || key.number > 255 || !reader.scalar(value)) return false;
        const uint32_t mask = uint32_t{1} << (key.number % 32);
        if (seen[key.number / 32] & mask) return false;
        seen[key.number / 32] |= mask;
        switch (key.number)
        {
        case 0:
            type_ok = equals(value, "TCPServerInterface") || equals(value, "BackboneInterface");
            break;
        case 1:
            if (value.kind != Kind::Boolean) return false;
            transport = value.number != 0;
            break;
        case 2:
            if (!hostname(value)) return false;
            result.host = value.bytes;
            result.host_size = value.size;
            break;
        case 6:
            if (value.kind != Kind::Integer || value.number == 0 || value.number > 65535) return false;
            result.port = static_cast<uint16_t>(value.number);
            break;
        case 7:
        case 8:
            // This client cannot join IFAC-protected interfaces yet.
            if (value.kind != Kind::Nil && (value.kind != Kind::String || value.size != 0)) return false;
            break;
        case 254:
            if (value.kind != Kind::Binary || value.size != 16) return false;
            result.transport_id = value.bytes;
            break;
        default:
            break;
        }
    }
    if (reader.pos != reader.size || !type_ok || !transport || !result.host || !result.port || !result.transport_id) return false;
    result.packed = reader.data;
    result.packed_size = reader.size;
    result.stamp = app_data + size - 32;
    out = result;
    return true;
}
} // namespace chat::reticulum
