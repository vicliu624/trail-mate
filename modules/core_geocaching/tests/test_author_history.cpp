#include "geocaching/storage/author_history.h"
int main()
{
    using namespace geocaching;
    uint8_t a[512]{}, b[512]{}, key[36]{}, public_key[64]{}, value[192]{};
    key[35] = 1;
    storage::LogicalState state(a, b, sizeof(a));
    RevisionHash hash; size_t length = 0;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, {}, value, sizeof(value), length)) return 1;
    storage::MutationView mutation{3, {key, 36}, {value, length}, false};
    auto apply = [&]()
    {
        const auto before = state.view();
        return state.apply(&mutation, 1, [&](const auto& candidate) { return storage::validateAuthorHistory(before, candidate); });
    };
    if (!apply() || !apply() || state.view().size() != 1) return 2;
    hash.bytes[0] = 1;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, {}, value, sizeof(value), length)) return 3;
    mutation.value = {value, length};
    if (apply() || state.view().size() != 1) return 4;
    mutation.erase = true; mutation.value = {};
    if (apply() || state.view().size() != 1) return 5;
    mutation.erase = false; mutation.value = {value, length}; key[35] = 2;
    if (!apply() || state.view().size() != 2) return 6;
    return 0;
}
