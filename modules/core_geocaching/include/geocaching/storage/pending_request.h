#pragma once
#include "geocaching/storage/cache_head.h"
#include "geocaching/storage/logical_state.h"
#include "geocaching/storage/task_record.h"

namespace geocaching::storage
{
enum class PendingRequestResult : uint8_t
{
    Ready,
    None,
    Corrupt
};
struct PendingRequestView
{
    std::array<uint8_t, 48> key{};
    Destination destination;
    RequestId request_id;
    ByteView request;
    ByteView task_id;
};

inline PendingRequestResult inspectPendingRequest(const LogicalState::View& state, const Destination& local,
                                                  ByteView key, ByteView value, PendingRequestView& out)
{
    out = {};
    OutgoingView outgoing;
    if (!decodeOutgoing(key, value, outgoing)) return PendingRequestResult::Corrupt;
    if (!outgoing.continue_intent || (outgoing.state != 0 && outgoing.state != 3) ||
        std::memcmp(key.data, local.bytes.data(), 16)) return PendingRequestResult::None;
    ByteView task_bytes;
    TaskView task;
    if (!state.find(10, outgoing.task_id, task_bytes) || !decodeTask(outgoing.task_id, task_bytes, task) ||
        !requestBelongsToTask(outgoing.task_id, task, key, outgoing)) return PendingRequestResult::Corrupt;
    if (!task.continue_intent || task.state > 2) return PendingRequestResult::None;
    if ((task.kind == 2 || task.kind == 4) && outgoing.install_generation)
    {
        ByteView head_bytes;
        CacheHeadView head;
        if (!state.find(2, task.cache_id, head_bytes) || !decodeCacheHead(task.cache_id, head_bytes, head)) return PendingRequestResult::Corrupt;
        if (head.install_generation != outgoing.install_generation) return PendingRequestResult::None;
    }
    std::memcpy(out.key.data(), key.data, 48);
    std::memcpy(out.destination.bytes.data(), key.data + 16, 16);
    std::memcpy(out.request_id.bytes.data(), key.data + 32, 16);
    out.request = outgoing.request;
    out.task_id = outgoing.task_id;
    return PendingRequestResult::Ready;
}

// Result borrows the state: copy request bytes before committing any state
// changes or admitting transport work. after_key permits bounded round-robin
// traversal; when exhausted the owner may start from an empty cursor.
inline PendingRequestResult nextPendingRequest(const LogicalState::View& state, const Destination& local,
                                               ByteView after_key, PendingRequestView& out)
{
    out = {};
    if (after_key.size && (!after_key.data || after_key.size != 48)) return PendingRequestResult::Corrupt;
    bool found = false;
    size_t cursor = 0;
    MutationView entry;
    while (state.next(cursor, entry))
    {
        if (entry.table != 5) continue;
        PendingRequestView candidate;
        const auto result = inspectPendingRequest(state, local, entry.key, entry.value, candidate);
        if (result == PendingRequestResult::Corrupt)
        {
            out = {};
            return result;
        }
        if (result != PendingRequestResult::Ready || (after_key.size && std::memcmp(entry.key.data, after_key.data, 48) <= 0)) continue;
        if (found && std::memcmp(entry.key.data, out.key.data(), 48) >= 0) continue;
        out = candidate;
        found = true;
    }
    return found ? PendingRequestResult::Ready : PendingRequestResult::None;
}
} // namespace geocaching::storage
