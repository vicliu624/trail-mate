#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/record_decoder.h"

namespace geocaching::storage
{
// Borrowed editor projection: neither text nor protocol-sized arrays are owned.
struct DraftView
{
    ByteView author, base_hash;
    uint64_t generation = 1;
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    std::string_view name, description, hint;
    uint8_t state = 0, difficulty_x2 = 2, terrain_x2 = 2, container_size = 0;
    bool has_coordinates = false;
};

enum class DraftUpdateCheck : uint8_t
{
    Allowed,
    NeedsRetainedPublication,
    InvalidGeneration,
    Conflict
};
// Both views must have passed decodeDraft. This policy is independent of the
// storage backend; signed publication retention is resolved separately by I/O.
inline DraftUpdateCheck checkDraftUpdate(const DraftView* previous, const DraftView& next, uint64_t expected_generation)
{
    if (expected_generation == UINT64_MAX || next.generation != expected_generation + 1)
        return DraftUpdateCheck::InvalidGeneration;
    if (!previous) return expected_generation ? DraftUpdateCheck::Conflict : DraftUpdateCheck::Allowed;
    if (previous->generation != expected_generation ||
        (previous->author.size && (next.author.size != previous->author.size ||
                                   std::memcmp(previous->author.data, next.author.data, previous->author.size))) ||
        next.base_hash.size != previous->base_hash.size ||
        (previous->base_hash.size && std::memcmp(next.base_hash.data, previous->base_hash.data, previous->base_hash.size)))
        return DraftUpdateCheck::Conflict;
    return previous->base_hash.size ? DraftUpdateCheck::NeedsRetainedPublication : DraftUpdateCheck::Allowed;
}

inline bool decodeDraft(ByteView key, ByteView value, DraftView& out)
{
    out = {};
    if (!key.data || key.size != 16 || !value.data || value.size > 32768) return false;
    protocol::CmpReader reader(value);
    DraftView draft;
    size_t fields = 0;
    uint64_t number = 0;
    int64_t coordinate = 0;
    if (!reader.array(fields, 4) || fields != 4) return false;
    auto optional = reader;
    if (optional.nil()) reader = optional;
    else if (!reader.binary(draft.author, 64) || draft.author.size != 64) return false;
    optional = reader;
    if (optional.nil()) reader = optional;
    else if (!reader.binary(draft.base_hash, 32) || draft.base_hash.size != 32) return false;
    if (!reader.array(fields, 9) || fields != 9 || !reader.unsignedInteger(number) || number > 2) return false;
    draft.state = static_cast<uint8_t>(number);
    optional = reader;
    if (optional.nil())
    {
        reader = optional;
        if (!reader.nil()) return false;
    }
    else
    {
        if (!reader.signedInteger(coordinate) || coordinate < -900000000 || coordinate > 900000000) return false;
        draft.latitude_e7 = static_cast<int32_t>(coordinate);
        if (!reader.signedInteger(coordinate) || coordinate < -1800000000 || coordinate >= 1800000000) return false;
        draft.longitude_e7 = static_cast<int32_t>(coordinate);
        draft.has_coordinates = true;
    }
    if (!reader.text(draft.name, kMaxNameBytes) || !protocol::validRecordText(draft.name, false, false) ||
        !reader.text(draft.description, kMaxDescriptionBytes) || !protocol::validRecordText(draft.description, true, false) ||
        !reader.text(draft.hint, kMaxHintBytes) || !protocol::validRecordText(draft.hint, true, false) ||
        !reader.unsignedInteger(number) || number < 2 || number > 10) return false;
    draft.difficulty_x2 = static_cast<uint8_t>(number);
    if (!reader.unsignedInteger(number) || number < 2 || number > 10) return false;
    draft.terrain_x2 = static_cast<uint8_t>(number);
    if (!reader.unsignedInteger(number) || number > 5) return false;
    draft.container_size = static_cast<uint8_t>(number);
    if (!reader.unsignedInteger(draft.generation) || !draft.generation || !reader.finished()) return false;
    out = draft;
    return true;
}

inline bool encodeDraft(ByteView key, const DraftView& draft, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !(draft.author.size ? writer.binary(draft.author) : writer.nil()) ||
        !(draft.base_hash.size ? writer.binary(draft.base_hash) : writer.nil()) ||
        !writer.array(9) || !writer.unsignedInteger(draft.state) ||
        !(draft.has_coordinates ? writer.signedInteger(draft.latitude_e7) && writer.signedInteger(draft.longitude_e7)
                                : writer.nil() && writer.nil()) ||
        !writer.text(draft.name) || !writer.text(draft.description) || !writer.text(draft.hint) ||
        !writer.unsignedInteger(draft.difficulty_x2) || !writer.unsignedInteger(draft.terrain_x2) ||
        !writer.unsignedInteger(draft.container_size) || !writer.unsignedInteger(draft.generation)) return false;
    DraftView checked;
    if (!decodeDraft(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
