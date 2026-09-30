#include "geocaching/domain/version_policy.h"
#include <cassert>

int main()
{
    using namespace geocaching;
    // Small metadata fixtures stand in for records already verified by crypto.
    protocol::VerifiedRecordView first, second;
    first.id.bytes.fill(1);
    second.id = first.id;
    first.hash.bytes.fill(2);
    second.hash.bytes.fill(3);
    first.record.revision = 1;
    second.record.revision = 2;
    second.record.previous_hash = {first.hash.bytes.data(), 32};
    assert(compareGeocacheVersions(first, first) == VersionRelation::Identical);
    assert(compareGeocacheVersions(first, second) == VersionRelation::NewerLinked);
    assert(compareGeocacheVersions(second, first) == VersionRelation::Older);
    second.record.updated_at = 1;
    first.record.updated_at = 1000;
    assert(compareGeocacheVersions(first, second) == VersionRelation::NewerLinked);
    second.record.revision = 3;
    assert(compareGeocacheVersions(first, second) == VersionRelation::NewerHistoryIncomplete);
    first.record.state = CacheState::Archived;
    assert(compareGeocacheVersions(first, second) == VersionRelation::Conflict);
    assert(compareGeocacheVersions(second, first) == VersionRelation::Conflict);
    first.record.state = CacheState::Active;
    second.record.revision = 1;
    assert(compareGeocacheVersions(first, second) == VersionRelation::Conflict);
    second.record.revision = 2;
    second.record.previous_hash = {};
    assert(compareGeocacheVersions(first, second) == VersionRelation::Conflict);
    second.record.previous_hash = {first.hash.bytes.data(), 32};
    second.record.created_at = 1;
    assert(compareGeocacheVersions(first, second) == VersionRelation::Conflict);
    second.id.bytes[0] = 9;
    assert(compareGeocacheVersions(first, second) == VersionRelation::DifferentCache);
}
