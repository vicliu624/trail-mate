#pragma once
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/install_record.h"
#include "geocaching/storage/volume_format.h"
#include "geocaching/usecase/gpx_install.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
class LinuxGeocachingVolumeLease;
// Blocking storage-owner metadata port. File proofs must come from the owning
// filesystem adapter. Metadata alone never establishes physical installation.
class LinuxGeocachingInstallJournal
{
  public:
    using TaskId = std::array<uint8_t, 16>;
    using Volume = ::geocaching::storage::VolumeInstance;
    using Hash = std::array<uint8_t, 32>;
    enum class Result
    {
        Ready,
        Missing,
        Invalid,
        Busy,
        Stale,
        Conflict,
        Corrupt,
        Unavailable,
        Error
    };
    struct Entry
    {
        TaskId task{};
        ::geocaching::InstallIdentity identity;
        bool active = false, continue_intent = false, has_record = false, has_old_file = false, cleanup_complete = false;
        ::geocaching::storage::InstallPhase phase = ::geocaching::storage::InstallPhase::Prepared;
        Hash new_file_hash{}, old_file_hash{};
        ::geocaching::RevisionHash previous_revision;
        bool has_current = false;
        ::geocaching::RevisionHash current_hash;
        Hash current_file_hash{};
        uint32_t highest_seen_revision = 0; // Installed history, not merely observed signed records.
    };
    // The volume ID is verified by the filesystem owner. Crypto must outlive
    // this port. A durable file database is required, not an in-memory URI.
    LinuxGeocachingInstallJournal(std::filesystem::path database, Volume volume,
                                  ::geocaching::protocol::RecordCrypto& crypto);
    ~LinuxGeocachingInstallJournal();
    Result open();
    // Reserve from a selected summary before receiving its signed payload.
    // History/signature/installability checks are mandatory at prepare/commit.
    Result begin(const TaskId&, const ::geocaching::GeocacheId&, const ::geocaching::RevisionHash&, uint64_t& generation);
    Result cancel(const TaskId&, uint64_t generation);
    Result prepare(const TaskId&, uint64_t generation, const Hash& new_file_hash, ::geocaching::ByteView old_file_hash);
    Result commit(const TaskId&, uint64_t generation, const Hash& observed_new_file_hash);
    Result finishCleanup(const TaskId&, uint64_t generation, const Hash& observed_new_file_hash);
    Result rolledBack(const TaskId&, uint64_t generation, ::geocaching::ByteView observed_old_file_hash);
    Result markConflict(const TaskId&, uint64_t generation);
    Result read(const TaskId&, Entry&);
    struct InstalledReference
    {
        Volume volume{};
        ::geocaching::GeocacheId id;
        ::geocaching::RevisionHash hash;
        Hash file_hash{};
        uint32_t highest_seen_revision = 0;
        bool valid = false;
    };
    struct InstalledPage
    {
        std::array<InstalledReference, 4> rows{};
        size_t offset = 0, count = 0, total = 0;
    };
    // Metadata only, never a usable-file claim. Bad row shapes remain visible
    // as invalid references rather than hiding other readable caches.
    Result installedPage(size_t offset, InstalledPage&);
    Result installedReference(const ::geocaching::GeocacheId&, InstalledReference&);
    // Stable task-key cursor, never an offset into a changing task list. Includes
    // cancelled prepared work and installed work with unfinished cleanup.
    Result nextRecovery(const TaskId* after, Entry&);
    enum class FileEffectMode
    {
        Stage,
        Install,
        Rollback,
        Cleanup
    };
    using FileEffect = Result (*)(void*, const Entry&, int root_descriptor);
    // Runs synchronously with the matching generation protected by a SQLite
    // write reservation and a matching acquired volume lease. Callback performs
    // file I/O only, retains no borrowed Entry/descriptor and never reenters this
    // object or another writer on the same database. Do not close the descriptor.
    // Failure/exception keeps the previously committed recovery journal.
    Result withFileEffect(const TaskId&, uint64_t generation, FileEffectMode, LinuxGeocachingVolumeLease&, FileEffect, void*);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
