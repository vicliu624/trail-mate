#pragma once
#include "geocaching/storage/saved_cache.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace platform::linux_runtime
{
// Blocking single-owner history repository. The caller authenticates the
// directory/response before admission. This port verifies authors and versions;
// it does not claim that a GPX file is installed or a publication was delivered.
class LinuxGeocachingRecordStore
{
  public:
    using Id = ::geocaching::GeocacheId;
    using Hash = ::geocaching::RevisionHash;
    enum class Result
    {
        Ready,
        Identical,
        Older,
        Conflict,
        Missing,
        Invalid,
        Corrupt,
        CryptoUnavailable,
        Error
    };
    struct Record
    {
        ::geocaching::storage::SavedCacheEntry summary;
        ::geocaching::CacheState state = ::geocaching::CacheState::Active;
        bool history_incomplete = true;
        std::vector<uint8_t> bytes;
    };
    // The verifier must outlive this storage owner.
    LinuxGeocachingRecordStore(std::filesystem::path database, ::geocaching::protocol::RecordCrypto& crypto);
    ~LinuxGeocachingRecordStore();
    Result open();
    // Expected identifiers come from the correlated response. Conflicts are
    // durably retained and never silently resolved by choosing a newer record.
    Result accept(const Id&, const Hash&, ::geocaching::ByteView signed_record);
    Result head(const Id&, Record&);
    Result read(const Id&, const Hash&, Record&);
    // Historical records deliberately have no local-copy removal API. Offline
    // installation/removal owns a separate reference and cannot erase history.
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
