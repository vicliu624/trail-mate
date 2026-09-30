#include "geocaching/storage/queued_request.h"
#include <memory>
int main()
{
    using namespace geocaching;
    RequestId request_id;
    Destination local, remote;
    GeocacheId cache;
    RevisionHash hash;
    std::array<uint8_t, 16> task{};
    uint8_t request[256]{};
    size_t request_size = 0;
    if (!protocol::encodeGetRequest(request_id, cache, &hash, nullptr, 8192, request, sizeof(request), request_size)) return 1;
    std::array<uint8_t, 512> value_buffer{};
    auto workspace = std::make_unique<storage::QueuedRequestWorkspace>(value_buffer.data(), value_buffer.size());
    std::array<uint8_t, 9000> output{};
    size_t size = 0;
    storage::RequestTaskTarget target{{cache.bytes.data(), 32}, {hash.bytes.data(), 32}, 1};
    if (!storage::encodeNewRequestTask(0, local, remote, request_id, task, 2, {request, request_size}, {},
                                       *workspace, output.data(), output.size(), size, target)) return 2;
    storage::MutationView mutations[2];
    storage::TransactionView transaction;
    if (!storage::decodeTransaction({output.data(), size}, 0, mutations, 2, transaction)) return 3;
    protocol::CmpReader record(mutations[1].value);
    size_t fields = 0;
    uint64_t kind = 0;
    ByteView stored_id, stored_hash;
    if (!record.array(fields, 6) || fields != 6 || !record.unsignedInteger(kind) || kind != 2 ||
        !record.binary(stored_id, 32) || stored_id.size != 32 || !record.binary(stored_hash, 32) || stored_hash.size != 32) return 4;
    target.install_generation = 0;
    if (storage::encodeNewRequestTask(0, local, remote, request_id, task, 2, {request, request_size}, {},
                                      *workspace, output.data(), output.size(), size, target)) return 5;
    target.install_generation = 1;
    if (storage::encodeNewRequestTask(0, local, remote, request_id, task, 1, {request, request_size}, {},
                                      *workspace, output.data(), output.size(), size, target)) return 6;
    return 0;
}
