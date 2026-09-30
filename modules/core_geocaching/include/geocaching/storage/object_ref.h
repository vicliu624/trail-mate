#pragma once
#include "geocaching/protocol/record_decoder.h"
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
    // Optional installed-record projection, derived from the verified response.
    // Legacy objects omit it. These keys permit direct reads, without history scans.
    std::string_view name;
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    ByteView saved_request, saved_task;
};
inline bool decodeObjectRef(ByteView key, ByteView value, ObjectRefView& out)
{
    out = {};
    if (!key.data || key.size != 32 || !value.data) return false;
    protocol::CmpReader reader(value);
    size_t count = 0;
    uint64_t revision = 0, state = 0, verified = 0;
    ObjectRefView object;
    if (!reader.array(count, 8) || (count != 7 && count != 8) || !reader.binary(object.cache_id, 32) || object.cache_id.size != 32 ||
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
    if (count == 8)
    {
        size_t fields = 0;
        int64_t coordinate = 0;
        if (!reader.array(fields, 5) || fields != 5 || !reader.text(object.name, kMaxNameBytes) ||
            !protocol::validRecordText(object.name, false, true) ||
            !reader.signedInteger(coordinate) || coordinate < -900000000 || coordinate > 900000000) return false;
        object.latitude_e7 = static_cast<int32_t>(coordinate);
        if (!reader.signedInteger(coordinate) || coordinate < -1800000000 || coordinate >= 1800000000) return false;
        object.longitude_e7 = static_cast<int32_t>(coordinate);
        if (!reader.binary(object.saved_request, 48) || object.saved_request.size != 48 ||
            !reader.binary(object.saved_task, 16) || object.saved_task.size != 16) return false;
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
    if (!writer.array(object.saved_request.size ? 8 : 7) || !writer.binary(object.cache_id) || !writer.unsignedInteger(object.revision) ||
        !(object.previous_hash.size ? writer.binary(object.previous_hash) : writer.nil()) ||
        !writer.unsignedInteger(static_cast<uint8_t>(object.state)) || !writer.unsignedInteger(object.created_at) ||
        !writer.unsignedInteger(1) || !(object.has_deadline ? encodeStoredTime(writer, object.retained_until) : writer.nil())) return false;
    if (object.saved_request.size &&
        (!writer.array(5) || !writer.text(object.name) || !writer.signedInteger(object.latitude_e7) ||
         !writer.signedInteger(object.longitude_e7) || !writer.binary(object.saved_request) || !writer.binary(object.saved_task))) return false;
    ObjectRefView checked;
    if (!decodeObjectRef(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
