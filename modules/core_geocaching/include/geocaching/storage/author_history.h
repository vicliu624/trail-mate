#pragma once
#include "geocaching/storage/author_issued.h"
#include "geocaching/storage/logical_state.h"

namespace geocaching::storage
{
// AuthorIssued is append-only within one volume lifecycle. Retrying the same
// issuance keeps its original record, including timestamp; changing contents or
// deleting a reservation would permit a second signed version at the same key.
inline bool validateAuthorHistory(const LogicalState::View& before, const LogicalState::View& candidate)
{
    size_t cursor = 0; MutationView entry;
    while (candidate.next(cursor, entry))
    {
        if (entry.table != 3) continue;
        AuthorIssuedView decoded;
        if (!decodeAuthorIssued(entry.key, entry.value, decoded)) return false;
    }
    cursor = 0;
    while (before.next(cursor, entry))
    {
        if (entry.table != 3) continue;
        ByteView retained;
        if (!candidate.find(3, entry.key, retained) || retained.size != entry.value.size ||
            std::memcmp(retained.data, entry.value.data, retained.size)) return false;
    }
    return true;
}
} // namespace geocaching::storage
