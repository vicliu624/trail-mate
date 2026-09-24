#include "geocaching/storage/pending_request.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/storage/task_references.h"
#include "geocaching/protocol/query_request.h"
#include <memory>
int main()
{
    using namespace geocaching;
    Destination local, remote; local.bytes[0] = 1; remote.bytes[0] = 2;
    RequestId id; id.bytes[0] = 3;
    uint8_t request[64]; size_t request_size = 0;
    if (!protocol::encodeCapabilitiesRequest(id, request, sizeof(request), request_size)) return 1;
    std::array<uint8_t, 512> value_buffer{};
    auto workspace = std::make_unique<storage::QueuedRequestWorkspace>(value_buffer.data(), value_buffer.size());
    uint8_t encoded[512]{}; size_t size = 0;
    if (!storage::encodeNewRequestTask(0, local, remote, id, {}, 3, {request, request_size}, {},
                                      *workspace, encoded, sizeof(encoded), size)) return 2;
    storage::MutationView mutations[2]; storage::TransactionView transaction;
    if (!storage::decodeTransaction({encoded, size}, 0, mutations, 2, transaction)) return 3;
    uint8_t a[512]{}, b[512]{}; storage::LogicalState state(a, b, sizeof(a));
    if (!state.apply(mutations, 2, storage::validateTaskReferences)) return 4;
    storage::PendingRequestView pending;
    if (storage::nextPendingRequest(state.view(), local, {}, pending) != storage::PendingRequestResult::Ready ||
        pending.destination.bytes != remote.bytes || pending.request_id.bytes != id.bytes || pending.request.size != request_size) return 5;
    if (storage::nextPendingRequest(state.view(), remote, {}, pending) != storage::PendingRequestResult::None) return 6;
    uint8_t task_key[16]{}; ByteView task_bytes; storage::TaskView task;
    if (!state.view().find(10, {task_key, 16}, task_bytes) || !storage::decodeTask({task_key, 16}, task_bytes, task)) return 7;
    task.state = 5; task.continue_intent = false;
    uint8_t stopped[256]{};
    if (!storage::encodeTask({task_key, 16}, task, stopped, sizeof(stopped), size)) return 8;
    storage::MutationView stop{10, {task_key, 16}, {stopped, size}, false};
    if (!state.apply(&stop, 1, storage::validateTaskReferences) ||
        storage::nextPendingRequest(state.view(), local, {}, pending) != storage::PendingRequestResult::None) return 9;
    return 0;
}
