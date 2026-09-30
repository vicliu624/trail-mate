#pragma once
#include "geocaching/domain/version_policy.h"
#include "geocaching/storage/cache_head.h"
#include "geocaching/storage/object_ref.h"

namespace geocaching::storage
{
// Incoming was independently authenticated. A current object is required only
// when the cache head already points at a retained verified version.
inline bool installableRecord(const CacheHeadView& head, const ObjectRefView* object,
                              const protocol::VerifiedRecordView& incoming)
{
    if (incoming.record.revision < head.highest_seen_revision || head.conflict_state == 2) return false;
    if (!head.current_hash.size) return true;
    if (head.current_hash.size != 32 || !object || object->cache_id.size != 32 ||
        std::memcmp(object->cache_id.data, incoming.id.bytes.data(), 32)) return false;
    protocol::VerifiedRecordView known;
    known.id = incoming.id;
    std::memcpy(known.hash.bytes.data(), head.current_hash.data, 32);
    known.record.revision = object->revision;
    known.record.state = object->state;
    known.record.created_at = object->created_at;
    known.record.previous_hash = object->previous_hash;
    const auto relation = compareGeocacheVersions(known, incoming);
    return relation == VersionRelation::Identical || relation == VersionRelation::NewerLinked;
}
} // namespace geocaching::storage
