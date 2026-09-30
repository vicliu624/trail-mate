#include "geocaching/protocol/query_request.h"
#include "geocaching/storage/queued_request.h"
#include <memory>

int main()
{
    using namespace geocaching;
    RequestId request_id;
    request_id.bytes[0] = 1;
    Destination local, remote;
    local.bytes[0] = 2;
    remote.bytes[0] = 3;
    std::array<uint8_t, 16> task_id{};
    task_id[0] = 4;
    uint8_t request[128]{};
    size_t request_size = 0;
    if (!protocol::encodeCapabilitiesRequest(request_id, request, sizeof(request), request_size)) return 1;
    std::array<uint8_t, 512> value_buffer{};
    auto scratch = std::make_unique<storage::QueuedRequestWorkspace>(value_buffer.data(), value_buffer.size());
    std::array<uint8_t, 9000> output{};
    size_t size = 0;
    storage::StoredTime time;
    time.boot_id[0] = 5;
    if (!storage::encodeNewRequestTask(7, local, remote, request_id, task_id, 3,
                                       {request, request_size}, time, *scratch, output.data(), output.size(), size)) return 2;
    storage::MutationView mutations[2];
    storage::TransactionView transaction;
    if (!storage::decodeTransaction({output.data(), size}, 7, mutations, 2, transaction) || transaction.count != 2 ||
        mutations[0].table != 5 || mutations[0].key.size != 48 || mutations[1].table != 10 || mutations[1].key.size != 16) return 3;
    protocol::CmpReader outgoing(mutations[0].value);
    size_t count = 0;
    ByteView stored_request;
    if (!outgoing.array(count, 7) || count != 7 || !outgoing.binary(stored_request, 8192) ||
        stored_request.size != request_size || std::memcmp(stored_request.data, request, request_size)) return 4;
    request_id.bytes[0] = 9;
    if (storage::encodeNewRequestTask(7, local, remote, request_id, task_id, 3,
                                      {request, request_size}, time, *scratch, output.data(), output.size(), size) ||
        size) return 5;
    return 0;
}
