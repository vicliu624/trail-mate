#include "waypoint/waypoint.h"
#include <cstring>

namespace waypoint
{
namespace
{
bool validName(const char (&name)[32])
{
    if (!name[0]) return false;
    unsigned index = 0;
    while (index < sizeof(name))
    {
        const auto lead = static_cast<uint8_t>(name[index++]);
        if (!lead) return true;
        if (lead < 0x80) continue;
        unsigned remaining;
        uint32_t scalar;
        uint32_t minimum;
        if (lead >= 0xc2 && lead <= 0xdf)
        {
            remaining = 1;
            scalar = lead & 0x1f;
            minimum = 0x80;
        }
        else if (lead >= 0xe0 && lead <= 0xef)
        {
            remaining = 2;
            scalar = lead & 0x0f;
            minimum = 0x800;
        }
        else if (lead >= 0xf0 && lead <= 0xf4)
        {
            remaining = 3;
            scalar = lead & 7;
            minimum = 0x10000;
        }
        else return false;
        if (index + remaining >= sizeof(name)) return false; // Reserve the terminator.
        while (remaining--)
        {
            const auto next = static_cast<uint8_t>(name[index++]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    }
    return false;
}
} // namespace
bool valid(const Record& record)
{
    return record.id != 0 && validName(record.name) &&
           record.latitude_e7 >= -900000000 && record.latitude_e7 <= 900000000 &&
           record.longitude_e7 >= -1800000000 && record.longitude_e7 <= 1800000000;
}

Result query(IStore& store, uint32_t after_id, uint8_t capacity, Page& out)
{
    out = {};
    if (!capacity || capacity > kPageCapacity || store.slotCount() > kMaxWaypoints) return Result::Invalid;
    Record record;
    for (uint16_t slot = 0; slot < store.slotCount(); ++slot)
    {
        const auto result = store.read(slot, record);
        if (result != Result::Ok || (record.id && !valid(record)))
        {
            out = {};
            return result == Result::Ok ? Result::Corrupt : result;
        }
        if (!record.id || record.id <= after_id) continue;
        uint8_t index = 0;
        while (index < out.count && out.rows[index].id < record.id) ++index;
        if (out.count == capacity) out.has_more = true;
        if (index == capacity) continue;
        if (out.count < capacity) ++out.count;
        for (uint8_t n = out.count - 1; n > index; --n) out.rows[n] = out.rows[n - 1];
        out.rows[index] = record;
    }
    return Result::Ok;
}

Result find(IStore& store, uint32_t id, Record& out)
{
    out = {};
    if (!id || store.slotCount() > kMaxWaypoints) return Result::Invalid;
    Record record;
    for (uint16_t slot = 0; slot < store.slotCount(); ++slot)
    {
        const auto result = store.read(slot, record);
        if (result != Result::Ok) return result;
        if (record.id && !valid(record)) return Result::Corrupt;
        if (record.id == id)
        {
            out = record;
            return Result::Ok;
        }
    }
    return Result::NotFound;
}
} // namespace waypoint
