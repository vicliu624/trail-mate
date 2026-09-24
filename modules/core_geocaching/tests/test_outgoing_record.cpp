#include "geocaching/storage/outgoing_record.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/protocol/query_request.h"
#include <memory>
int main()
{
    using namespace geocaching;
    RequestId id; id.bytes[0] = 1;
    uint8_t request[64]; size_t request_size = 0;
    if (!protocol::encodeCapabilitiesRequest(id, request, sizeof(request), request_size)) return 1;
    std::array<uint8_t, 512> value_buffer{};
    auto scratch = std::make_unique<storage::QueuedRequestWorkspace>(value_buffer.data(), value_buffer.size());
    std::array<uint8_t, 9000> bytes; size_t size = 0;
    storage::StoredTime time; time.monotonic_ms = 123; time.boot_id[0] = 7;
    if (!storage::encodeNewRequestTask(0, {}, {}, id, {}, 3, {request, request_size}, time,
                                       *scratch, bytes.data(), bytes.size(), size)) return 2;
    storage::MutationView mutations[2]; storage::TransactionView transaction;
    if (!storage::decodeTransaction({bytes.data(), size}, 0, mutations, 2, transaction)) return 3;
    storage::OutgoingView out;
    if (!storage::decodeOutgoing(mutations[0].key, mutations[0].value, out) || out.state != 0 ||
        !out.continue_intent || out.created.monotonic_ms != 123 || out.created.boot_id[0] != 7 ||
        out.created.has_utc || out.install_generation || out.request.size != request_size) return 4;
    for (size_t n = 0; n < mutations[0].value.size; ++n)
        if (storage::decodeOutgoing(mutations[0].key, {mutations[0].value.data, n}, out) || out.request.data) return 5;
    std::array<uint8_t, 48> wrong_key{};
    if (storage::decodeOutgoing({wrong_key.data(), wrong_key.size()}, mutations[0].value, out)) return 6;
    return 0;
}
