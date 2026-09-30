#pragma once
#include "geocaching/protocol/verify_record.h"
#include <cstring>

namespace geocaching
{
enum class VersionRelation : std::uint8_t
{
    DifferentCache,
    Identical,
    Older,
    NewerLinked,
    NewerHistoryIncomplete,
    Conflict,
};

// Call only for independently authenticated records. A repository must compare
// against relevant retained history as well, not just its current head.
inline VersionRelation compareGeocacheVersions(const protocol::VerifiedRecordView& known,
                                               const protocol::VerifiedRecordView& incoming)
{
    if (known.id.bytes != incoming.id.bytes) return VersionRelation::DifferentCache;
    if (known.hash.bytes == incoming.hash.bytes) return VersionRelation::Identical;
    const auto& a = known.record;
    const auto& b = incoming.record;
    if (a.revision == b.revision || a.created_at != b.created_at) return VersionRelation::Conflict;
    const bool newer = b.revision > a.revision;
    const auto& lower = newer ? known : incoming;
    const auto& higher = newer ? incoming : known;
    if (lower.record.state == CacheState::Archived) return VersionRelation::Conflict;
    if (static_cast<std::uint64_t>(lower.record.revision) + 1 == higher.record.revision)
    {
        if (higher.record.previous_hash.size != 32 || !higher.record.previous_hash.data ||
            std::memcmp(higher.record.previous_hash.data, lower.hash.bytes.data(), 32) != 0)
            return VersionRelation::Conflict;
        return newer ? VersionRelation::NewerLinked : VersionRelation::Older;
    }
    return newer ? VersionRelation::NewerHistoryIncomplete : VersionRelation::Older;
}
} // namespace geocaching
