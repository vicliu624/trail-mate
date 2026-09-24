#pragma once
#include "geocaching/storage/stored_time.h"
#include "geocaching/storage/task_record.h"
#include "geocaching/storage/transaction.h"

namespace geocaching::storage
{
// Contiguous codec callers supply a bounded value buffer. The device store
// instead encodes into candidate storage and does not allocate this workspace.
struct QueuedRequestWorkspace
{
    QueuedRequestWorkspace(uint8_t* data, size_t capacity) : outgoing(data), outgoing_capacity(capacity) {}
    uint8_t* outgoing;
    size_t outgoing_capacity;
    std::array<uint8_t, 256> task{};
};

struct RequestTaskTarget
{
    ByteView cache_id;
    ByteView revision_hash;
    uint64_t install_generation = 0;
};

// Builds a new one-request task and Outgoing atomically. The caller has already
// validated the operation-specific request body. Later retries/pages update the
// existing task; they must not call this constructor to overwrite task history.
inline bool describeNewRequestTask(
    const Destination& local, const Destination& remote,
    const RequestId& request_id, const std::array<uint8_t, 16>& task_id,
    uint8_t task_kind, ByteView request, const StoredTime& time,
    std::array<uint8_t, 48>& key, OutgoingView& outgoing, TaskView& task,
    const RequestTaskTarget& target = {})
{
    outgoing = {};
    task = {};
    if (task_kind < 1 || task_kind > 4 || !request.data || request.size > kMaxApplicationBytes ||
        (time.utc_trusted && !time.has_utc) || (time.has_utc && time.utc_seconds > 253402300799ULL)) return false;
    if (task_kind == 3)
    {
        if (target.cache_id.size || target.revision_hash.size || target.install_generation) return false;
    }
    else if (!target.cache_id.data || target.cache_id.size != 32 || !target.revision_hash.data ||
             target.revision_hash.size != 32 || (task_kind == 2 && target.install_generation == 0)) return false;
    protocol::CmpReader reader(request);
    size_t fields = 0;
    uint64_t version = 0, type = 0, operation = 0, budget = 0;
    ByteView encoded_id;
    if (!reader.array(fields, 6) || fields != 6 || !reader.unsignedInteger(version) || version != 1 ||
        !reader.unsignedInteger(type) || type != 0 || !reader.unsignedInteger(operation) ||
        operation > 4 || !reader.binary(encoded_id, 16) || encoded_id.size != 16 ||
        std::memcmp(encoded_id.data, request_id.bytes.data(), 16) || !reader.unsignedInteger(budget) ||
        budget < 512 || budget > 8192 || reader.finished()) return false;
    const bool compatible = operation == 0 ||
                            (task_kind == 1 && operation == 1) ||
                            (task_kind == 2 && operation == 3) ||
                            (task_kind == 3 && operation == 2) ||
                            (task_kind == 4 && (operation == 3 || operation == 4));
    if (!compatible) return false;
    std::memcpy(key.data(), local.bytes.data(), 16);
    std::memcpy(key.data() + 16, remote.bytes.data(), 16);
    std::memcpy(key.data() + 32, request_id.bytes.data(), 16);
    outgoing.request = request;
    outgoing.continue_intent = true;
    outgoing.task_id = {task_id.data(), task_id.size()};
    outgoing.install_generation = target.install_generation;
    outgoing.created = time;
    task.kind = task_kind;
    task.cache_id = target.cache_id;
    task.revision_hash = target.revision_hash;
    task.continue_intent = true;
    task.request_count = 1;
    task.requests[0] = {key.data(), key.size()};
    return true;
}

// Compatibility codec path, sharing the same record construction and encoders.
inline bool prepareNewRequestTask(
    const Destination& local, const Destination& remote,
    const RequestId& request_id, const std::array<uint8_t, 16>& task_id,
    uint8_t task_kind, ByteView request, const StoredTime& time,
    QueuedRequestWorkspace& scratch,
    std::array<uint8_t, 48>& key, MutationView (&mutations)[2],
    const RequestTaskTarget& target = {})
{
    mutations[0] = {};
    mutations[1] = {};
    OutgoingView outgoing;
    TaskView task;
    if (!describeNewRequestTask(local, remote, request_id, task_id, task_kind, request, time, key, outgoing, task, target))
        return false;
    size_t outgoing_size = 0, task_size = 0;
    if (!encodeOutgoing({key.data(), key.size()}, outgoing, scratch.outgoing, scratch.outgoing_capacity, outgoing_size) ||
        !encodeTask(outgoing.task_id, task, scratch.task.data(), scratch.task.size(), task_size)) return false;
    mutations[0] = {5, {key.data(), key.size()}, {scratch.outgoing, outgoing_size}, false};
    mutations[1] = {10, outgoing.task_id, {scratch.task.data(), task_size}, false};
    return true;
}
// Both entry points share one task construction and one transaction encoder.
inline bool encodeNewRequestTask(uint64_t previous_sequence,
                                 const Destination& local, const Destination& remote,
                                 const RequestId& request_id, const std::array<uint8_t, 16>& task_id,
                                 uint8_t task_kind, ByteView request, const StoredTime& time,
                                 QueuedRequestWorkspace& scratch,
                                 uint8_t* output, size_t capacity, size_t& written,
                                 const RequestTaskTarget& target = {})
{
    written = 0;
    std::array<uint8_t, 48> key{};
    MutationView mutations[2];
    return prepareNewRequestTask(local, remote, request_id, task_id, task_kind, request, time,
                                 scratch, key, mutations, target) &&
           encodeTransaction(previous_sequence, mutations, 2, output, capacity, written);
}
} // namespace geocaching::storage
