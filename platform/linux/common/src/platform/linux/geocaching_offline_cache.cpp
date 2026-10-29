#include "geocaching_offline_cache.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/geocaching_gpx_file_port.h"
#include "platform/linux/runtime_paths.h"
#include <cstdio>
#include <sqlite3.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace platform::linux_runtime
{
using Journal = LinuxGeocachingInstallJournal;
using Records = LinuxGeocachingRecordStore;
using Lease = LinuxGeocachingVolumeLease;
using Port = LinuxGeocachingGpxFilePort;
namespace gc = ::ui::geocaching;
namespace
{
bool binding(const std::filesystem::path& path, const Journal::Volume& volume, bool create)
{
    sqlite3* db = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
#if defined(SQLITE_OPEN_NOFOLLOW)
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    if (sqlite3_open_v2(path.string().c_str(), &db, flags, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 2000);
    sqlite3_stmt* statement = nullptr;
    bool ready = false;
    if (create)
    {
        const char* schema = "PRAGMA synchronous=FULL; BEGIN IMMEDIATE; CREATE TABLE linux_geocaching_store(singleton INTEGER PRIMARY KEY CHECK(singleton=1),version INTEGER NOT NULL,volume BLOB NOT NULL CHECK(length(volume)=16));";
        if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, "INSERT INTO linux_geocaching_store VALUES(1,1,?)", -1, &statement, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_blob(statement, 1, volume.data(), 16, SQLITE_TRANSIENT);
            ready = sqlite3_step(statement) == SQLITE_DONE;
        }
        if (statement)
        {
            sqlite3_finalize(statement);
            statement = nullptr;
        }
        ready = ready && sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
        if (!ready) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    else if (sqlite3_prepare_v2(db, "SELECT version,volume FROM linux_geocaching_store WHERE singleton=1", -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* bytes = sqlite3_column_blob(statement, 1);
        ready = sqlite3_column_int64(statement, 0) == 1 && bytes && sqlite3_column_bytes(statement, 1) == 16 && !std::memcmp(bytes, volume.data(), 16);
    }
    if (statement)
    {
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    if (ready && !create)
    {
        const char* tables = "SELECT COUNT(*) FROM sqlite_schema WHERE type='table' AND name IN('linux_geocaching_store','linux_geocaching_records','linux_geocaching_heads','linux_geocaching_install_state','linux_geocaching_install_tasks')";
        ready = sqlite3_prepare_v2(db, tables, -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int64(statement, 0) == 5;
    }
    if (statement) sqlite3_finalize(statement);
    sqlite3_close(db);
    return ready;
}
} // namespace
struct GeocachingOfflineCache::Impl
{
    explicit Impl(std::filesystem::path storage) : storage_root(std::move(storage)) {}
    void reset()
    {
        journal.reset();
        records.reset();
        lease.reset();
        ready = false;
    }
    bool recover(const std::atomic_bool& stopping)
    {
        bool attention = false;
        Journal::TaskId cursor{};
        bool has_cursor = false;
        while (!stopping)
        {
            Journal::Entry entry;
            const auto found = journal->nextRecovery(has_cursor ? &cursor : nullptr, entry);
            if (found == Journal::Result::Missing) break;
            if (found != Journal::Result::Ready)
            {
                attention = true;
                break;
            }
            Port port(entry.task, *lease, *journal, *records, crypto);
            using Effect = ::geocaching::InstallEffectResult;
            bool installed = entry.phase == ::geocaching::storage::InstallPhase::Installed;
            bool completed = false;
            if (entry.phase == ::geocaching::storage::InstallPhase::Prepared)
            {
                if (entry.continue_intent && port.stage(entry.identity) == Effect::Complete)
                {
                    ::geocaching::GpxInstall flow(entry.identity);
                    while (!stopping && flow.step() < ::geocaching::InstallStep::Complete) flow.advance(port);
                    installed = flow.installed();
                }
                if (!installed && !stopping)
                {
                    Journal::Entry observed;
                    installed = journal->read(entry.task, observed) == Journal::Result::Ready && observed.phase == ::geocaching::storage::InstallPhase::Installed;
                    if (!installed)
                    {
                        Effect result;
                        do
                        {
                            result = port.rollback(entry.identity);
                        } while (!stopping && result == Effect::Pending);
                        completed = result == Effect::Complete;
                    }
                }
            }
            if (installed && !stopping)
            {
                Effect result;
                do
                {
                    result = port.cleanup(entry.identity);
                } while (!stopping && result == Effect::Pending);
                completed = result == Effect::Complete;
            }
            if (!completed) attention = true;
            cursor = entry.task;
            has_cursor = true;
        }
        message = attention ? "Offline caches; recovery needs attention" : "Offline caches";
        return !stopping;
    }
    std::filesystem::path database, storage_root, cache_root;
    LinuxGeocachingCrypto crypto;
    std::unique_ptr<Lease> lease;
    std::unique_ptr<Records> records;
    std::unique_ptr<Journal> journal;
    Journal::Volume volume{};
    Journal::Volume initializing{}; // Only this process may finish its known fresh initialization.
    bool ready = false;
    std::string message = "Opening offline cache storage...";
};
GeocachingOfflineCache::GeocachingOfflineCache(std::filesystem::path storage_root)
    : impl_(std::make_unique<Impl>(std::move(storage_root))) {}
GeocachingOfflineCache::~GeocachingOfflineCache() = default;
bool GeocachingOfflineCache::open(const std::atomic_bool& stopping)
{
    impl_->reset();
    if (impl_->storage_root.empty())
    {
        impl_->message = "Offline storage is not configured";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(impl_->storage_root, error) || error)
    {
        impl_->message = "Offline cache storage is unavailable";
        return false;
    }
    if (!resolve_child_under_root(impl_->storage_root, "trailmate/geocaching", impl_->cache_root))
    {
        impl_->message = "Offline cache path is unavailable";
        return false;
    }
    std::filesystem::create_directories(impl_->cache_root, error);
    if (error)
    {
        impl_->message = "Cannot create the cache directory";
        return false;
    }
    impl_->lease = std::make_unique<Lease>(impl_->cache_root);
    bool fresh_volume = false;
    auto acquired = impl_->lease->acquire(impl_->volume);
    if (acquired == Lease::Result::Missing)
    {
        Journal::Volume fresh{};
#if defined(TRAIL_MATE_HAS_OPENSSL)
        if (RAND_bytes(fresh.data(), fresh.size()) != 1)
        {
            impl_->message = "Cannot initialize offline storage";
            return false;
        }
#else
        impl_->message = "Cache verification is unavailable";
        return false;
#endif
        const auto initialized = impl_->lease->initialize(fresh);
        if (initialized == Lease::Result::Ready || initialized == Lease::Result::Exists)
        {
            if (initialized == Lease::Result::Ready) impl_->initializing = fresh;
            acquired = impl_->lease->acquire(impl_->volume);
        }
        else acquired = initialized;
    }
    if (acquired != Lease::Result::Ready)
    {
        impl_->message = acquired == Lease::Result::Busy ? "Offline storage is in use by another application" : acquired == Lease::Result::Unsupported ? "Offline storage format is not supported"
                                                                                                                                                       : "Offline storage needs attention; files retained";
        return false;
    }
    fresh_volume = impl_->initializing != Journal::Volume{} && impl_->initializing == impl_->volume;
    if (!resolve_child_under_root(impl_->cache_root, ".state/native-cache.sqlite3", impl_->database))
    {
        impl_->message = "Offline cache metadata path is unavailable";
        impl_->lease->release();
        return false;
    }
    for (const auto* suffix : {"-journal", "-wal", "-shm"})
    {
        const auto sidecar = std::filesystem::symlink_status(impl_->database.string() + suffix, error);
        if (!error && std::filesystem::is_symlink(sidecar))
        {
            impl_->message = "Offline cache metadata path needs review";
            impl_->lease->release();
            return false;
        }
        error.clear();
    }
    const bool already_bound = binding(impl_->database, impl_->volume, false);
    if (!fresh_volume && !already_bound)
    {
        impl_->message = "Offline cache metadata needs review; files retained";
        impl_->lease->release();
        return false;
    }
    impl_->records = std::make_unique<Records>(impl_->database, impl_->crypto);
    impl_->journal = std::make_unique<Journal>(impl_->database, impl_->volume, impl_->crypto);
    if (impl_->records->open() != Records::Result::Ready || impl_->journal->open() != Journal::Result::Ready)
    {
        impl_->message = "Offline cache metadata is unavailable";
        impl_->reset();
        return false;
    }
    if (!already_bound && !binding(impl_->database, impl_->volume, true))
    {
        impl_->message = "Offline cache initialization did not complete; files retained";
        impl_->reset();
        return false;
    }
    impl_->initializing = {};
    impl_->ready = impl_->recover(stopping);
    return impl_->ready;
}
bool GeocachingOfflineCache::healthy() const
{
    return impl_->ready && impl_->lease && impl_->lease->check(impl_->volume) == Lease::Result::Ready;
}
const std::string& GeocachingOfflineCache::status() const { return impl_->message; }
void GeocachingOfflineCache::window(size_t offset, Window& out)
{
    out = {};
    out.offset = offset;
    out.status = impl_->message;
    if (!impl_->ready) return;
    if (!healthy())
    {
        out.status = "Offline cache storage is unavailable";
        return;
    }
    Journal::InstalledPage page;
    if (impl_->journal->installedPage(offset, page) != Journal::Result::Ready)
    {
        out.status = "Offline cache metadata needs attention";
        return;
    }
    out.ready = true;
    out.volume = impl_->volume;
    out.total = page.total;
    out.count = page.count;
    for (size_t i = 0; i < page.count; ++i)
    {
        const auto& reference = page.rows[i];
        out.references[i] = reference;
        auto& item = out.rows[i];
        item.id = reference.id.bytes;
        item.revision_hash = reference.hash.bytes;
        std::snprintf(item.name.data(), item.name.size(), "Unavailable cache");
        std::snprintf(item.detail.data(), item.detail.size(), "Offline copy is unavailable");
        if (!reference.valid) continue;
        Records::Record retained;
        if (impl_->records->read(reference.id, reference.hash, retained) == Records::Result::Ready) item.name = retained.summary.name;
        Records::Record verified;
        if (Port::readInstalled(reference, *impl_->lease, *impl_->records, impl_->crypto, verified) != Journal::Result::Ready) continue;
        item.name = verified.summary.name;
        item.latitude_e7 = verified.summary.latitude_e7;
        item.longitude_e7 = verified.summary.longitude_e7;
        item.has_coordinates = true;
        item.downloaded = true;
        item.state = static_cast<uint8_t>(verified.state);
        std::snprintf(item.detail.data(), item.detail.size(), "Local copy; revision %lu", static_cast<unsigned long>(verified.summary.revision));
    }
}
bool GeocachingOfflineCache::detail(const Journal::InstalledReference& reference, Records::Record& out, std::string& error)
{
    out = {};
    error.clear();
    if (!healthy() || reference.volume != impl_->volume)
    {
        error = "Offline cache storage is unavailable";
        return false;
    }
    Journal::InstalledReference current;
    if (impl_->journal->installedReference(reference.id, current) != Journal::Result::Ready || current.hash.bytes != reference.hash.bytes || current.file_hash != reference.file_hash)
    {
        error = "Cache changed; refresh the list";
        return false;
    }
    if (Port::readInstalled(current, *impl_->lease, *impl_->records, impl_->crypto, out) != Journal::Result::Ready)
    {
        error = "Offline copy is unavailable";
        return false;
    }
    return true;
}
} // namespace platform::linux_runtime
