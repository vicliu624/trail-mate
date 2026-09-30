#pragma once
#include "geocaching/storage/stored_time.h"

namespace geocaching::storage
{
struct OutgoingView
{
    ByteView request;
    uint8_t state = 0;
    bool continue_intent = false;
    ByteView task_id;
    uint64_t install_generation = 0;
    StoredTime created;
    ByteView terminal_data;
};

inline bool decodeOutgoing(ByteView key, ByteView value, OutgoingView& out)
{
    out = {};
    if (!key.data || key.size != 48 || !value.data || value.size > 32768) return false;
    OutgoingView candidate;
    protocol::CmpReader reader(value);
    size_t fields = 0;
    uint64_t state = 0, intent = 0;
    if (!reader.array(fields, 7) || fields != 7 || !reader.binary(candidate.request, 8192) ||
        candidate.request.size == 0 || !reader.unsignedInteger(state) || state > 5 ||
        !reader.unsignedInteger(intent) || intent > 1 || !reader.binary(candidate.task_id, 16) || candidate.task_id.size != 16) return false;
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.unsignedInteger(candidate.install_generation) || candidate.install_generation == 0) return false;
    if (!decodeStoredTime(reader, candidate.created)) return false;
    nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(candidate.terminal_data, 8192) || candidate.terminal_data.size == 0) return false;
    if (!reader.finished() || (state == 4 && candidate.terminal_data.size == 0) ||
        (state < 4 && candidate.terminal_data.size != 0)) return false;
    protocol::CmpReader request(candidate.request);
    uint64_t version = 0, type = 0, operation = 0, budget = 0;
    ByteView request_id;
    if (!request.array(fields, 6) || fields != 6 || !request.unsignedInteger(version) || version != 1 ||
        !request.unsignedInteger(type) || type != 0 || !request.unsignedInteger(operation) || operation > 4 ||
        !request.binary(request_id, 16) || request_id.size != 16 || std::memcmp(request_id.data, key.data + 32, 16) ||
        !request.unsignedInteger(budget) || budget < 512 || budget > 8192 || request.finished()) return false;
    candidate.state = static_cast<uint8_t>(state);
    candidate.continue_intent = intent != 0;
    out = candidate;
    return true;
}
// Updates reuse original request/time views. Input views must not overlap output.
inline bool encodeOutgoing(ByteView key, const OutgoingView& value,
                           uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!value.request.data || value.request.size > 8192 || value.terminal_data.size > 8192) return false;
    protocol::CmpWriter writer(output, capacity < 32768 ? capacity : 32768);
    if (!writer.array(7) || !writer.binary(value.request) || !writer.unsignedInteger(value.state) ||
        !writer.unsignedInteger(value.continue_intent ? 1 : 0) || !writer.binary(value.task_id) ||
        !(value.install_generation ? writer.unsignedInteger(value.install_generation) : writer.nil()) ||
        !encodeStoredTime(writer, value.created) ||
        !(value.terminal_data.size ? writer.binary(value.terminal_data) : writer.nil())) return false;
    OutgoingView checked;
    if (!decodeOutgoing(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
