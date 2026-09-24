#pragma once
#include "geocaching/storage/outgoing_record.h"

namespace geocaching::storage
{
struct TaskView
{
    uint8_t kind = 0;
    ByteView cache_id;
    ByteView revision_hash;
    uint8_t state = 0;
    bool continue_intent = false;
    std::array<ByteView, 3> requests{};
    size_t request_count = 0;
};

inline bool decodeTask(ByteView key, ByteView value, TaskView& out)
{
    out = {};
    if (!key.data || key.size != 16 || !value.data || value.size > 32768) return false;
    TaskView candidate;
    protocol::CmpReader reader(value);
    size_t count = 0;
    uint64_t kind = 0, state = 0, intent = 0;
    if (!reader.array(count, 6) || count != 6 || !reader.unsignedInteger(kind) || kind < 1 || kind > 4) return false;
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(candidate.cache_id, 32) || candidate.cache_id.size != 32) return false;
    nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(candidate.revision_hash, 32) || candidate.revision_hash.size != 32) return false;
    if (!reader.unsignedInteger(state) || state > 5 || !reader.unsignedInteger(intent) || intent > 1 ||
        !reader.array(candidate.request_count, 3)) return false;
    for (size_t i = 0; i < candidate.request_count; ++i)
    {
        auto& request = candidate.requests[i];
        if (!reader.binary(request, 48) || request.size != 48) return false;
        for (size_t j = 0; j < i; ++j)
            if (std::memcmp(candidate.requests[j].data, request.data, 48) == 0) return false;
    }
    if (!reader.finished()) return false;
    candidate.kind = static_cast<uint8_t>(kind);
    candidate.state = static_cast<uint8_t>(state);
    candidate.continue_intent = intent != 0;
    out = candidate;
    return true;
}

inline bool encodeTask(ByteView key, const TaskView& task, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (task.request_count > task.requests.size()) return false;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(6) || !writer.unsignedInteger(task.kind) ||
        !(task.cache_id.size ? writer.binary(task.cache_id) : writer.nil()) ||
        !(task.revision_hash.size ? writer.binary(task.revision_hash) : writer.nil()) ||
        !writer.unsignedInteger(task.state) || !writer.unsignedInteger(task.continue_intent ? 1 : 0) ||
        !writer.array(static_cast<uint32_t>(task.request_count))) return false;
    for (size_t i = 0; i < task.request_count; ++i) if (!writer.binary(task.requests[i])) return false;
    TaskView checked;
    if (!decodeTask(key, {output, writer.size()}, checked)) return false;
    written = writer.size(); return true;
}

// Both records must first pass their decoders. Recovery checks every task child
// and every Outgoing parent using the transaction's resulting logical tables.
inline bool requestBelongsToTask(ByteView task_key, const TaskView& task,
                                 ByteView request_key, const OutgoingView& outgoing)
{
    if (!task_key.data || task_key.size != 16 || !request_key.data || request_key.size != 48 ||
        !outgoing.task_id.data || outgoing.task_id.size != 16 || task.request_count > task.requests.size() ||
        std::memcmp(task_key.data, outgoing.task_id.data, 16)) return false;
    for (size_t i = 0; i < task.request_count; ++i)
        if (task.requests[i].data && task.requests[i].size == 48 &&
            std::memcmp(task.requests[i].data, request_key.data, 48) == 0) return true;
    return false;
}
} // namespace geocaching::storage
