#pragma once
#include "geocaching/protocol/verify_record.h"

namespace geocaching::storage
{
// Owned list/map metadata. No record text or borrowed response survives a read.
struct SavedCacheEntry
{
    std::array<uint8_t, 32> id{}, hash{};
    std::array<char, 97> name{};
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    uint32_t revision = 0;
};
struct SavedCacheRecord : SavedCacheEntry
{
    std::array<uint8_t, 32> file_hash{};
};
static_assert(sizeof(SavedCacheEntry) <= 180, "Saved rows retain only list/map metadata");
static_assert(sizeof(SavedCacheRecord) <= 216, "A saved candidate adds only the installed file proof");

inline protocol::VerificationResult verifySavedCache(ByteView signed_cache, protocol::RecordCrypto& crypto,
                                                     uint8_t* scratch, size_t capacity, SavedCacheRecord& out)
{
    GeocacheId id;
    RevisionHash hash;
    id.bytes = out.id;
    hash.bytes = out.hash;
    protocol::VerifiedRecordView verified;
    const auto result = protocol::verifyGeocache(signed_cache, crypto, scratch, capacity, verified, &id, &hash);
    if (result != protocol::VerificationResult::Valid) return result;
    out.name.fill(0);
    std::memcpy(out.name.data(), verified.record.name.data(), verified.record.name.size());
    out.latitude_e7 = verified.record.latitude_e7;
    out.longitude_e7 = verified.record.longitude_e7;
    out.revision = verified.record.revision;
    return result;
}
} // namespace geocaching::storage
