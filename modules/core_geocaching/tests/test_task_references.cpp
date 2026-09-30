#include "geocaching/protocol/query_request.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/storage/task_references.h"
#include <memory>
int main()
{
    using namespace geocaching;
    RequestId id;
    uint8_t request[64];
    size_t request_size = 0;
    if (!protocol::encodeCapabilitiesRequest(id, request, sizeof(request), request_size)) return 1;
    std::array<uint8_t, 512> value_buffer{};
    auto scratch = std::make_unique<storage::QueuedRequestWorkspace>(value_buffer.data(), value_buffer.size());
    std::array<uint8_t, 9000> bytes{};
    size_t size = 0;
    if (!storage::encodeNewRequestTask(0, {}, {}, id, {}, 3, {request, request_size}, {},
                                       *scratch, bytes.data(), bytes.size(), size)) return 2;
    storage::MutationView mutations[2];
    storage::TransactionView transaction;
    if (!storage::decodeTransaction({bytes.data(), size}, 0, mutations, 2, transaction)) return 3;
    uint8_t first[512]{}, second[512]{};
    storage::LogicalState state(first, second, sizeof(first));
    if (state.apply(mutations, 1, storage::validateTaskReferences) || state.view().size()) return 4;
    if (!state.apply(mutations, 2, storage::validateTaskReferences) || state.view().size() != 2) return 5;
    storage::MutationView remove_task{10, mutations[1].key, {}, true};
    if (state.apply(&remove_task, 1, storage::validateTaskReferences) || state.view().size() != 2) return 6;
    storage::MutationView remove_request{5, mutations[0].key, {}, true};
    if (state.apply(&remove_request, 1, storage::validateTaskReferences) || state.view().size() != 2) return 7;
    storage::MutationView remove_both[] = {remove_task, remove_request};
    if (!state.apply(remove_both, 2, storage::validateTaskReferences) || state.view().size()) return 8;
    // Reuse the entire source read workspace before resolving its references.
    // Both directions must retain only owned keys, never borrowed source bytes.
    for (size_t source = 0; source < 2; ++source)
    {
        uint8_t source_key[48]{};
        uint8_t source_value[512]{};
        const auto& row = mutations[source];
        std::memcpy(source_key, row.key.data, row.key.size);
        std::memcpy(source_value, row.value.data, row.value.size);
        storage::TaskReferenceCheck check;
        if (!check.begin({row.table, {source_key, row.key.size}, {source_value, row.value.size}, false})) return 9;
        std::memset(source_key, 0xcc, sizeof(source_key));
        std::memset(source_value, 0xcc, sizeof(source_value));
        uint8_t table = 0;
        ByteView key;
        const auto& target = mutations[1 - source];
        if (check.complete() || !check.next(table, key) || table != target.table ||
            key.size != target.key.size || std::memcmp(key.data, target.key.data, key.size)) return 10;
        if (!check.accept(target.value) || !check.complete() || check.next(table, key)) return 11;
        if (!check.begin(row) || check.accept({}) || check.complete() || check.next(table, key)) return 12;
    }
    return 0;
}
