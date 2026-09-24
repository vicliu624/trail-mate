#pragma once
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/storage/draft_record.h"
#include "geocaching/storage/task_record.h"
#include "geocaching/storage/transaction.h"

namespace geocaching::storage
{
struct DraftPublication
{
    uint32_t latest_revision = 0, confirmed_revision = 0;
    bool pending = false, stopped = false, local_changes = false;
    bool base_retained = false;
};

// Read-only projection of the verified local request ledger, not a substitute
// for signature verification at submission/recovery. Owns no payload or list.
template <class View>
DraftPublication draftPublication(const View& view, ByteView draft_id, const DraftView& draft)
{
    DraftPublication result;
    if (!draft_id.data || draft_id.size != 16 || draft.author.size != 64) return result;
    size_t cursor = 0;
    MutationView row;
    while (view.next(cursor, row))
    {
        if (row.table != 5) continue;
        OutgoingView outgoing;
        TaskView task;
        ByteView value;
        if (!decodeOutgoing(row.key, row.value, outgoing) || !view.find(10, outgoing.task_id, value) ||
            !decodeTask(outgoing.task_id, value, task) || task.kind != 1 ||
            !requestBelongsToTask(outgoing.task_id, task, row.key, outgoing) || task.cache_id.size != 32 || task.revision_hash.size != 32) continue;
        RequestId request;
        std::memcpy(request.bytes.data(), row.key.data + 32, 16);
        protocol::PublishRequestView publish;
        if (!protocol::decodePublishRequest(outgoing.request, request, publish)) continue;
        protocol::CmpReader reader(publish.signed_cache);
        size_t fields = 0;
        ByteView encoded, signature;
        RecordView record;
        if (!reader.array(fields, 2) || fields != 2 || !reader.binary(encoded, kMaxRecordBytes) ||
            !reader.binary(signature, 64) || !reader.finished() || !protocol::decodeGeocacheRecord(encoded, record) ||
            std::memcmp(record.author_public_key.data, draft.author.data, 64) ||
            std::memcmp(record.creation_nonce.data, draft_id.data, 16)) continue;
        if (draft.base_hash.size == 32 && !std::memcmp(draft.base_hash.data, task.revision_hash.data, 32)) result.base_retained = true;
        if (record.revision > result.latest_revision)
        {
            result.latest_revision = record.revision;
            result.pending = result.stopped = false;
            result.local_changes = !draft.has_coordinates || record.state != static_cast<CacheState>(draft.state) ||
                                   record.latitude_e7 != draft.latitude_e7 || record.longitude_e7 != draft.longitude_e7 ||
                                   record.name != draft.name || record.description != draft.description || record.hint != draft.hint ||
                                   record.difficulty_x2 != draft.difficulty_x2 || record.terrain_x2 != draft.terrain_x2 ||
                                   record.container_size != static_cast<ContainerSize>(draft.container_size);
        }
        if (outgoing.state == 4)
        {
            GeocacheId id;
            RevisionHash hash;
            std::memcpy(id.bytes.data(), task.cache_id.data, 32);
            std::memcpy(hash.bytes.data(), task.revision_hash.data, 32);
            protocol::PublishDisposition disposition;
            if (protocol::decodePublishResponse(outgoing.terminal_data, request, id, hash, record.revision, record.state, disposition) &&
                record.revision > result.confirmed_revision) result.confirmed_revision = record.revision;
        }
        else if (record.revision == result.latest_revision)
        {
            const bool active = outgoing.continue_intent && task.continue_intent && task.state != 5;
            result.pending = result.pending || active;
            result.stopped = result.stopped || !active;
        }
    }
    return result;
}
} // namespace geocaching::storage
