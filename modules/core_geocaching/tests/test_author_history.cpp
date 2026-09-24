#include "geocaching/storage/author_history.h"
#include "geocaching/storage/publication_history.h"
int main()
{
    using namespace geocaching;
    uint8_t a[512]{}, b[512]{}, key[36]{}, public_key[64]{}, value[192]{};
    key[35] = 1;
    storage::LogicalState state(a, b, sizeof(a));
    RevisionHash hash;
    size_t length = 0;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, {}, value, sizeof(value), length)) return 1;
    storage::MutationView mutation{3, {key, 36}, {value, length}, false};
    auto apply = [&]()
    {
        const auto before = state.view();
        return state.apply(&mutation, 1, [&](const auto& candidate)
                           { return storage::validateAuthorHistory(before, candidate); });
    };
    if (!apply() || !apply() || state.view().size() != 1) return 2;
    hash.bytes[0] = 1;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, {}, value, sizeof(value), length)) return 3;
    mutation.value = {value, length};
    if (apply() || state.view().size() != 1) return 4;
    mutation.erase = true;
    mutation.value = {};
    if (apply() || state.view().size() != 1) return 5;
    mutation.erase = false;
    mutation.value = {value, length};
    key[35] = 2;
    if (!apply() || state.view().size() != 2) return 6;
    GeocacheId cache;
    storage::PublicationHistory history;
    storage::PublicationHistoryScan empty;
    if (!empty.finish(0, history) || history.latest_revision || empty.finish(1, history)) return 7;
    storage::StoredTime issued;
    issued.has_utc = true;
    storage::PublicationHistoryScan complete, missing, wrong_author, no_utc, duplicate;
    for (const uint8_t revision : {uint8_t(2), uint8_t(4), uint8_t(1), uint8_t(3)})
    {
        key[35] = revision;
        hash.bytes.fill(revision);
        issued.utc_seconds = 1000 + revision;
        if (!storage::encodeAuthorIssued(hash, {public_key, 64}, issued, value, sizeof(value), length) ||
            !complete.consume({key, 36}, {value, length}, cache, {public_key, 64})) return 8;
        if (revision != 2 && !missing.consume({key, 36}, {value, length}, cache, {public_key, 64})) return 9;
    }
    if (!complete.finish(3, history) || history.latest_revision != 4 || history.confirmed_revision != 3 ||
        history.created_at != 1001 || history.latest_time.utc_seconds != 1004 || history.latest_hash.bytes[0] != 4 ||
        history.previous_hash.bytes[0] != 3 || missing.finish(1, history)) return 10;
    public_key[0] = 1;
    if (wrong_author.consume({key, 36}, {value, length}, cache, {public_key, 64})) return 11;
    public_key[0] = 0;
    if (!duplicate.consume({key, 36}, {value, length}, cache, {public_key, 64}) ||
        duplicate.consume({key, 36}, {value, length}, cache, {public_key, 64})) return 12;
    issued.has_utc = false;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, issued, value, sizeof(value), length) ||
        no_utc.consume({key, 36}, {value, length}, cache, {public_key, 64})) return 13;
    return 0;
}
