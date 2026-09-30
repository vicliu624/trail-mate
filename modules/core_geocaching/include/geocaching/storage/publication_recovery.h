#pragma once
#include "geocaching/storage/pending_request.h"

namespace geocaching::storage
{
struct PublicationRecoveryFilter
{
    Destination local, remote;
    GeocacheId cache;
    RevisionHash hash;
    bool has_remote = false, has_cache = false, has_hash = false;

    bool matchesRequest(ByteView key) const
    {
        return key.data && key.size == 48 && !std::memcmp(key.data, local.bytes.data(), 16) &&
               (!has_remote || !std::memcmp(key.data + 16, remote.bytes.data(), 16));
    }
    bool same(const PublicationRecoveryFilter& other) const
    {
        return local.bytes == other.local.bytes && has_remote == other.has_remote && has_cache == other.has_cache && has_hash == other.has_hash &&
               (!has_remote || remote.bytes == other.remote.bytes) && (!has_cache || cache.bytes == other.cache.bytes) && (!has_hash || hash.bytes == other.hash.bytes);
    }
};
// Metadata is owned. Request/response borrow the read lease until released.
struct PublicationRecoveryView
{
    std::array<uint8_t, 48> key{};
    std::array<uint8_t, 16> task{};
    GeocacheId cache;
    RevisionHash hash;
    StoredTime created;
    ByteView request, response;
    bool confirmed = false;
};
enum class PublicationRecoveryResult : uint8_t
{
    None,
    Ready,
    Invalid
};

inline PublicationRecoveryResult publicationRecoveryCandidate(const PublicationRecoveryFilter& filter, ByteView key,
                                                              const OutgoingView& outgoing, const TaskView& task,
                                                              PublicationRecoveryView& out)
{
    if (!filter.matchesRequest(key) || task.kind != 1) return PublicationRecoveryResult::None;
    if (!requestBelongsToTask(outgoing.task_id, task, key, outgoing) || task.cache_id.size != 32 || task.revision_hash.size != 32)
        return PublicationRecoveryResult::Invalid;
    if ((filter.has_cache && std::memcmp(task.cache_id.data, filter.cache.bytes.data(), 32)) ||
        (filter.has_hash && std::memcmp(task.revision_hash.data, filter.hash.bytes.data(), 32))) return PublicationRecoveryResult::None;
    if (outgoing.state > 4 || (outgoing.state == 4 && !filter.has_cache) ||
        (outgoing.state < 4 && (!outgoing.continue_intent || !task.continue_intent || task.state == 3 || task.state == 5)))
        return PublicationRecoveryResult::None;
    std::memcpy(out.key.data(), key.data, 48);
    std::memcpy(out.task.data(), outgoing.task_id.data, 16);
    std::memcpy(out.cache.bytes.data(), task.cache_id.data, 32);
    std::memcpy(out.hash.bytes.data(), task.revision_hash.data, 32);
    out.created = outgoing.created;
    out.confirmed = outgoing.state == 4;
    out.request = out.response = {};
    return PublicationRecoveryResult::Ready;
}
inline bool preferPublicationRecovery(const PublicationRecoveryView& candidate, const PublicationRecoveryView& selected)
{
    if (candidate.confirmed != selected.confirmed) return candidate.confirmed;
    return candidate.key < selected.key;
}
template <class View>
PublicationRecoveryResult readLogicalPublicationRecovery(const View& view, const PublicationRecoveryFilter& filter,
                                                         PublicationRecoveryView& out)
{
    out = {};
    size_t cursor = 0;
    MutationView row;
    bool found = false;
    while (view.next(cursor, row))
    {
        if (row.table != 5 || !filter.matchesRequest(row.key)) continue;
        OutgoingView outgoing;
        TaskView task;
        ByteView value;
        if (!decodeOutgoing(row.key, row.value, outgoing) || !view.find(10, outgoing.task_id, value) ||
            !decodeTask(outgoing.task_id, value, task)) return PublicationRecoveryResult::Invalid;
        PublicationRecoveryView candidate;
        const auto result = publicationRecoveryCandidate(filter, row.key, outgoing, task, candidate);
        if (result == PublicationRecoveryResult::Invalid) return result;
        if (result == PublicationRecoveryResult::Ready && (!found || preferPublicationRecovery(candidate, out)))
        {
            out = candidate;
            out.request = outgoing.request;
            out.response = candidate.confirmed ? outgoing.terminal_data : ByteView{};
            found = true;
        }
    }
    return found ? PublicationRecoveryResult::Ready : PublicationRecoveryResult::None;
}
} // namespace geocaching::storage
