#include "geocaching/storage/task_record.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/protocol/query_request.h"
#include <memory>
int main()
{
    using namespace geocaching;
    RequestId id; uint8_t request[64]; size_t request_size = 0;
    if (!protocol::encodeCapabilitiesRequest(id, request, sizeof(request), request_size)) return 1;
    std::array<uint8_t, 512> value_buffer{};
    auto workspace = std::make_unique<storage::QueuedRequestWorkspace>(value_buffer.data(), value_buffer.size());
    std::array<uint8_t, 9000> bytes{}; size_t size = 0;
    std::array<uint8_t, 16> task_id{}; task_id[0] = 7;
    if (!storage::encodeNewRequestTask(0, {}, {}, id, task_id, 3, {request, request_size}, {},
                                      *workspace, bytes.data(), bytes.size(), size)) return 2;
    storage::MutationView mutations[2]; storage::TransactionView transaction;
    storage::OutgoingView outgoing; storage::TaskView task;
    if (!storage::decodeTransaction({bytes.data(), size}, 0, mutations, 2, transaction) ||
        !storage::decodeOutgoing(mutations[0].key, mutations[0].value, outgoing) ||
        !storage::decodeTask(mutations[1].key, mutations[1].value, task) ||
        !storage::requestBelongsToTask(mutations[1].key, task, mutations[0].key, outgoing)) return 3;
    task_id[0] = 8;
    if (storage::requestBelongsToTask({task_id.data(), 16}, task, mutations[0].key, outgoing)) return 4;
    for (size_t n = 0; n < mutations[1].value.size; ++n)
        if (storage::decodeTask(mutations[1].key, {mutations[1].value.data, n}, task) || task.request_count) return 5;
    return 0;
}
