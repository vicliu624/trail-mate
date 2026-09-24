#pragma once
#include "geocaching/storage/stored_time.h"

namespace geocaching::storage
{
struct ObjectRefView
{
    ByteView cache_id, previous_hash;
    uint32_t revision = 0;
    CacheState state = CacheState::Active;
    uint64_t created_at = 0;
    StoredTime retained_until;
    bool has_deadline = false;
};
inline bool decodeObjectRef(ByteView key, ByteView value, ObjectRefView& out)
{
    out = {};
    if (!key.data || key.size != 32 || !value.data) return false;
    protocol::CmpReader reader(value);
    size_t count = 0;
    uint64_t revision = 0, state = 0, verified = 0;
    ObjectRefView object;
    if (!reader.array(count, 7) || count != 7 || !reader.binary(object.cache_id, 32) || object.cache_id.size != 32 ||
        !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX) return false;
    auto optional = reader;
    if (optional.nil()) reader = optional;
    else if (!reader.binary(object.previous_hash, 32) || object.previous_hash.size != 32) return false;
    if ((revision == 1) != (object.previous_hash.size == 0) || !reader.unsignedInteger(state) || state > 2 ||
        !reader.unsignedInteger(object.created_at) || object.created_at > 253402300799ULL ||
        !reader.unsignedInteger(verified) || verified != 1) return false;
    optional = reader;
    if (optional.nil()) reader = optional;
    else
    {
        if (!decodeStoredTime(reader, object.retained_until)) return false;
        object.has_deadline = true;
    }
    if (!reader.finished()) return false;
    object.revision = static_cast<uint32_t>(revision);
    object.state = static_cast<CacheState>(state);
    out = object;
    return true;
}
inline bool encodeObjectRef(ByteView key, const ObjectRefView& object, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(7) || !writer.binary(object.cache_id) || !writer.unsignedInteger(object.revision) ||
        !(object.previous_hash.size ? writer.binary(object.previous_hash) : writer.nil()) ||
        !writer.unsignedInteger(static_cast<uint8_t>(object.state)) || !writer.unsignedInteger(object.created_at) ||
        !writer.unsignedInteger(1) || !(object.has_deadline ? encodeStoredTime(writer, object.retained_until) : writer.nil())) return false;
    ObjectRefView checked;
    if (!decodeObjectRef(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
