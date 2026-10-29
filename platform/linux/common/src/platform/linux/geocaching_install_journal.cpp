#include "platform/linux/geocaching_install_journal.h"
#include "geocaching/storage/installable_record.h"
#include "platform/linux/geocaching_record_store.h"
#include "platform/linux/geocaching_volume_lease.h"
#include <algorithm>
#include <limits>
#include <sqlite3.h>
#include <vector>

namespace platform::linux_runtime
{
namespace gc = ::geocaching;
namespace
{
using Journal = LinuxGeocachingInstallJournal;
using Result = Journal::Result;
using Phase = gc::storage::InstallPhase;
using Records = LinuxGeocachingRecordStore;
bool nonzero(const Journal::TaskId& id)
{
    return std::any_of(id.begin(), id.end(), [](uint8_t value)
                       { return value != 0; });
}
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement()
    {
        if (value) sqlite3_finalize(value);
    }
    bool prepare(sqlite3* db, const char* sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
    void blob(int index, const void* data, size_t size) { sqlite3_bind_blob(value, index, data, static_cast<int>(size), SQLITE_TRANSIENT); }
    void counter(int index, uint64_t value)
    {
        std::array<uint8_t, 8> bytes{};
        for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(value >> ((7 - i) * 8));
        blob(index, bytes.data(), bytes.size());
    }
};
bool counter(sqlite3_stmt* row, int column, uint64_t& value)
{
    value = 0;
    if (sqlite3_column_bytes(row, column) != 8) return false;
    const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(row, column));
    if (!bytes) return false;
    for (unsigned i = 0; i < 8; ++i) value = (value << 8) | bytes[i];
    return value != 0;
}
bool copy(sqlite3_stmt* row, int column, void* output, size_t size)
{
    if (sqlite3_column_bytes(row, column) != static_cast<int>(size)) return false;
    const auto* bytes = sqlite3_column_blob(row, column);
    if (!bytes) return false;
    std::memcpy(output, bytes, size);
    return true;
}
struct Transaction
{
    sqlite3* db;
    bool active;
    Transaction(sqlite3* value, bool write) : db(value), active(sqlite3_exec(db, write ? "BEGIN IMMEDIATE" : "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK) {}
    ~Transaction()
    {
        if (active) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    Result commit()
    {
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return Result::Error;
        active = false;
        return Result::Ready;
    }
};
Result map(Records::Result result)
{
    switch (result)
    {
    case Records::Result::Ready:
        return Result::Ready;
    case Records::Result::Missing:
        return Result::Missing;
    case Records::Result::Conflict:
        return Result::Conflict;
    case Records::Result::CryptoUnavailable:
        return Result::Unavailable;
    case Records::Result::Error:
        return Result::Error;
    default:
        return Result::Corrupt;
    }
}
bool trustedRecord(const Records::Record& stored, gc::RecordView& out)
{
    gc::protocol::CmpReader reader({stored.bytes.data(), stored.bytes.size()});
    size_t fields = 0;
    gc::ByteView encoded, signature;
    return reader.array(fields, 2) && fields == 2 && reader.binary(encoded, gc::kMaxRecordBytes) &&
           reader.binary(signature, 64) && signature.size == 64 && reader.finished() && gc::protocol::decodeGeocacheRecord(encoded, out);
}
} // namespace
struct LinuxGeocachingInstallJournal::Impl
{
    Impl(std::filesystem::path path, Volume volume_id, gc::protocol::RecordCrypto& verifier)
        : database(std::move(path)), volume(volume_id), crypto(verifier) {}
    ~Impl()
    {
        if (db) sqlite3_close(db);
    }
    struct State
    {
        bool exists = false, has_current = false;
        uint64_t generation = 0;
        uint32_t highest_seen_revision = 0;
        TaskId task{};
        gc::RevisionHash hash;
        Hash file_hash{};
    };
    void volumeKey(Statement& query) { query.blob(1, volume.data(), volume.size()); }
    Result state(const gc::GeocacheId& id, State& out)
    {
        out = {};
        Statement query;
        if (!query.prepare(db, "SELECT generation,active_task,installed_hash,file_hash,highest_seen_revision FROM linux_geocaching_install_state WHERE volume=? AND id=?")) return Result::Error;
        volumeKey(query);
        query.blob(2, id.bytes.data(), 32);
        const auto step = sqlite3_step(query.value);
        if (step == SQLITE_DONE) return Result::Missing;
        if (step != SQLITE_ROW) return Result::Error;
        if (!counter(query.value, 0, out.generation) || !copy(query.value, 1, out.task.data(), 16) || !nonzero(out.task)) return Result::Corrupt;
        const bool hash_null = sqlite3_column_type(query.value, 2) == SQLITE_NULL, file_null = sqlite3_column_type(query.value, 3) == SQLITE_NULL;
        if (hash_null != file_null) return Result::Corrupt;
        if (!hash_null && (!copy(query.value, 2, out.hash.bytes.data(), 32) || !copy(query.value, 3, out.file_hash.data(), 32))) return Result::Corrupt;
        const auto highest = sqlite3_column_int64(query.value, 4);
        if (highest < 0 || highest > UINT32_MAX || (!hash_null && !highest)) return Result::Corrupt;
        out.highest_seen_revision = static_cast<uint32_t>(highest);
        out.exists = true;
        out.has_current = !hash_null;
        return Result::Ready;
    }
    Result entry(const TaskId& task, Entry& out, State& current)
    {
        out = {};
        current = {};
        Statement query;
        if (!query.prepare(db, "SELECT id,hash,generation,continue_intent,record,previous_revision FROM linux_geocaching_install_tasks WHERE volume=? AND task=?")) return Result::Error;
        volumeKey(query);
        query.blob(2, task.data(), 16);
        const auto step = sqlite3_step(query.value);
        if (step == SQLITE_DONE) return Result::Missing;
        if (step != SQLITE_ROW) return Result::Error;
        out.task = task;
        if (!nonzero(task) || !copy(query.value, 0, out.identity.id.bytes.data(), 32) || !copy(query.value, 1, out.identity.hash.bytes.data(), 32) ||
            !counter(query.value, 2, out.identity.generation)) return Result::Corrupt;
        const auto continued = sqlite3_column_int64(query.value, 3);
        if (continued != 0 && continued != 1) return Result::Corrupt;
        out.continue_intent = continued;
        if (sqlite3_column_type(query.value, 4) != SQLITE_NULL)
        {
            const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 4));
            const auto size = sqlite3_column_bytes(query.value, 4);
            gc::storage::InstallRecordView record;
            if (!bytes || size <= 0 || size > 256 || !gc::storage::decodeInstallRecord({task.data(), 16}, {bytes, static_cast<size_t>(size)}, record) ||
                record.generation != out.identity.generation || std::memcmp(record.cache_id.data, out.identity.id.bytes.data(), 32) ||
                std::memcmp(record.revision_hash.data, out.identity.hash.bytes.data(), 32)) return Result::Corrupt;
            out.has_record = true;
            out.phase = record.phase;
            out.cleanup_complete = record.cleanup_complete;
            std::copy_n(record.new_file_hash.data, 32, out.new_file_hash.data());
            out.has_old_file = record.old_file_hash.size != 0;
            if (out.has_old_file) std::copy_n(record.old_file_hash.data, 32, out.old_file_hash.data());
            if (out.phase != Phase::Prepared && out.continue_intent) return Result::Corrupt;
        }
        const bool previous_null = sqlite3_column_type(query.value, 5) == SQLITE_NULL;
        if (out.has_old_file == previous_null) return Result::Corrupt;
        if (out.has_old_file && !copy(query.value, 5, out.previous_revision.bytes.data(), 32)) return Result::Corrupt;
        const auto loaded = state(out.identity.id, current);
        if (loaded != Result::Ready) return loaded == Result::Missing ? Result::Corrupt : loaded;
        if (out.identity.generation > current.generation) return Result::Corrupt;
        out.active = current.generation == out.identity.generation && current.task == task;
        if ((current.generation == out.identity.generation) != (current.task == task)) return Result::Corrupt;
        out.has_current = current.has_current;
        out.current_hash = current.hash;
        out.current_file_hash = current.file_hash;
        out.highest_seen_revision = current.highest_seen_revision;
        return Result::Ready;
    }
    Result eligible(const gc::InstallIdentity& identity, const State& current, uint32_t* installed_revision = nullptr)
    {
        Records::Record latest, selected;
        auto result = map(records->head(identity.id, latest));
        if (result != Result::Ready) return result == Result::Missing && current.highest_seen_revision ? Result::Corrupt : result;
        result = map(records->read(identity.id, identity.hash, selected));
        if (result != Result::Ready) return result;
        if (selected.summary.revision < current.highest_seen_revision) return Result::Stale;
        gc::protocol::VerifiedRecordView incoming;
        if (!trustedRecord(selected, incoming.record)) return Result::Corrupt;
        incoming.id = identity.id;
        incoming.hash = identity.hash;
        gc::storage::CacheHeadView head;
        head.install_generation = identity.generation;
        head.highest_seen_revision = current.highest_seen_revision;
        gc::storage::ObjectRefView object;
        Records::Record previous;
        gc::RecordView prior;
        if (current.has_current)
        {
            result = map(records->read(identity.id, current.hash, previous));
            if (result != Result::Ready) return result == Result::Missing ? Result::Corrupt : result;
            if (!trustedRecord(previous, prior) || prior.revision != current.highest_seen_revision) return Result::Corrupt;
            head.current_hash = {current.hash.bytes.data(), 32};
            object.cache_id = {identity.id.bytes.data(), 32};
            object.revision = prior.revision;
            object.state = prior.state;
            object.created_at = prior.created_at;
            object.previous_hash = prior.previous_hash;
        }
        if (!gc::storage::installableRecord(head, current.has_current ? &object : nullptr, incoming)) return Result::Conflict;
        if (installed_revision) *installed_revision = selected.summary.revision;
        return Result::Ready;
    }
    Result reservable(const gc::GeocacheId& id, const gc::RevisionHash& hash, const State& current)
    {
        Records::Record latest, selected;
        const auto latest_result = map(records->head(id, latest));
        if (latest_result == Result::Missing) return current.highest_seen_revision ? Result::Corrupt : Result::Ready;
        if (latest_result != Result::Ready) return latest_result;
        const auto selected_result = map(records->read(id, hash, selected));
        if (selected_result == Result::Missing) return Result::Ready; // Get response has not arrived yet.
        if (selected_result != Result::Ready) return selected_result;
        return selected.summary.revision < current.highest_seen_revision ? Result::Stale : Result::Ready;
    }
    Result write(const Entry& value)
    {
        std::array<uint8_t, 256> bytes{};
        size_t size = 0;
        if (value.has_record)
        {
            gc::storage::InstallRecordView record;
            record.cache_id = {value.identity.id.bytes.data(), 32};
            record.revision_hash = {value.identity.hash.bytes.data(), 32};
            record.new_file_hash = {value.new_file_hash.data(), 32};
            if (value.has_old_file) record.old_file_hash = {value.old_file_hash.data(), 32};
            record.generation = value.identity.generation;
            record.phase = value.phase;
            record.cleanup_complete = value.cleanup_complete;
            if (!gc::storage::encodeInstallRecord({value.task.data(), 16}, record, bytes.data(), bytes.size(), size)) return Result::Corrupt;
        }
        Statement update;
        if (!update.prepare(db, "UPDATE linux_geocaching_install_tasks SET continue_intent=?,record=?,previous_revision=? WHERE volume=? AND task=? AND generation=?")) return Result::Error;
        sqlite3_bind_int(update.value, 1, value.continue_intent);
        if (value.has_record) update.blob(2, bytes.data(), size);
        else sqlite3_bind_null(update.value, 2);
        if (value.has_old_file) update.blob(3, value.previous_revision.bytes.data(), 32);
        else sqlite3_bind_null(update.value, 3);
        update.blob(4, volume.data(), 16);
        update.blob(5, value.task.data(), 16);
        update.counter(6, value.identity.generation);
        return sqlite3_step(update.value) == SQLITE_DONE && sqlite3_changes(db) == 1 ? Result::Ready : Result::Error;
    }
    std::filesystem::path database;
    Volume volume;
    gc::protocol::RecordCrypto& crypto;
    std::unique_ptr<Records> records;
    sqlite3* db = nullptr;
};
LinuxGeocachingInstallJournal::LinuxGeocachingInstallJournal(std::filesystem::path database, Volume volume, gc::protocol::RecordCrypto& crypto)
    : impl_(std::make_unique<Impl>(std::move(database), volume, crypto)) {}
LinuxGeocachingInstallJournal::~LinuxGeocachingInstallJournal() = default;
Result LinuxGeocachingInstallJournal::open()
{
    if (impl_->db) return Result::Ready;
    if (!nonzero(impl_->volume) || impl_->database.empty() || impl_->database == ":memory:" || impl_->database.string().rfind("file:", 0) == 0) return Result::Invalid;
    std::error_code error;
    impl_->database = std::filesystem::absolute(impl_->database, error);
    if (error) return Result::Error;
    impl_->records = std::make_unique<Records>(impl_->database, impl_->crypto);
    auto result = map(impl_->records->open());
    if (result != Result::Ready) return result;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(impl_->database.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return Result::Error;
    }
    sqlite3_busy_timeout(db, 2000);
    const char* schema = "PRAGMA synchronous=FULL; BEGIN IMMEDIATE;"
                         "CREATE TABLE IF NOT EXISTS linux_geocaching_install_state(volume BLOB NOT NULL CHECK(length(volume)=16),id BLOB NOT NULL CHECK(length(id)=32),"
                         "generation BLOB NOT NULL CHECK(length(generation)=8),active_task BLOB NOT NULL CHECK(length(active_task)=16),"
                         "installed_hash BLOB CHECK(installed_hash IS NULL OR length(installed_hash)=32),file_hash BLOB CHECK(file_hash IS NULL OR length(file_hash)=32),"
                         "highest_seen_revision INTEGER NOT NULL CHECK(highest_seen_revision BETWEEN 0 AND 4294967295),"
                         "CHECK((installed_hash IS NULL)=(file_hash IS NULL)),PRIMARY KEY(volume,id)) WITHOUT ROWID;"
                         "CREATE TABLE IF NOT EXISTS linux_geocaching_install_tasks(volume BLOB NOT NULL CHECK(length(volume)=16),task BLOB NOT NULL CHECK(length(task)=16),"
                         "id BLOB NOT NULL CHECK(length(id)=32),hash BLOB NOT NULL CHECK(length(hash)=32),generation BLOB NOT NULL CHECK(length(generation)=8),"
                         "continue_intent INTEGER NOT NULL CHECK(continue_intent IN(0,1)),record BLOB CHECK(record IS NULL OR length(record) BETWEEN 1 AND 256),"
                         "previous_revision BLOB CHECK(previous_revision IS NULL OR length(previous_revision)=32),PRIMARY KEY(volume,task)) WITHOUT ROWID; COMMIT;";
    if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return Result::Error;
    }
    impl_->db = db;
    return Result::Ready;
}
Result LinuxGeocachingInstallJournal::begin(const TaskId& task, const gc::GeocacheId& id, const gc::RevisionHash& hash, uint64_t& generation)
{
    generation = 0;
    if (!impl_->db) return Result::Error;
    if (!nonzero(task)) return Result::Invalid;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry existing;
    Entry previous_active;
    Impl::State current;
    auto result = impl_->entry(task, existing, current);
    if (result == Result::Ready)
    {
        if (existing.identity.id.bytes != id.bytes || existing.identity.hash.bytes != hash.bytes) return Result::Conflict;
        if (!existing.active || (!existing.continue_intent && (!existing.has_record || existing.phase != Phase::Installed))) return Result::Stale;
        generation = existing.identity.generation;
        return Result::Ready;
    }
    if (result != Result::Missing) return result;
    result = impl_->state(id, current);
    if (result != Result::Ready && result != Result::Missing) return result;
    if (current.exists)
    {
        Entry active;
        Impl::State checked;
        result = impl_->entry(current.task, active, checked);
        if (result != Result::Ready || !active.active) return result == Result::Ready || result == Result::Missing ? Result::Corrupt : result;
        if (active.identity.id.bytes != id.bytes || checked.generation != current.generation || checked.task != current.task) return Result::Corrupt;
        if (active.has_record && (active.phase == Phase::Prepared || active.phase == Phase::ConflictExternal ||
                                  (active.phase == Phase::Installed && !active.cleanup_complete))) return Result::Busy;
        if (current.generation == UINT64_MAX) return Result::Invalid;
        previous_active = active;
    }
    gc::InstallIdentity identity{id, hash, current.generation + 1};
    result = impl_->reservable(id, hash, current);
    if (result != Result::Ready) return result;
    // Finish reads on the history connection before dirtying this connection's
    // pages, avoiding cross-connection read waits after a SQLite cache spill.
    if (previous_active.continue_intent)
    {
        previous_active.continue_intent = false;
        result = impl_->write(previous_active);
        if (result != Result::Ready) return result;
    }
    Statement insert, state;
    if (!insert.prepare(impl_->db, "INSERT INTO linux_geocaching_install_tasks VALUES(?,?,?,?,?,1,NULL,NULL)") ||
        !state.prepare(impl_->db, "INSERT INTO linux_geocaching_install_state VALUES(?,?,?,?,NULL,NULL,0) ON CONFLICT(volume,id) DO UPDATE SET generation=excluded.generation,active_task=excluded.active_task")) return Result::Error;
    impl_->volumeKey(insert);
    insert.blob(2, task.data(), 16);
    insert.blob(3, id.bytes.data(), 32);
    insert.blob(4, hash.bytes.data(), 32);
    insert.counter(5, identity.generation);
    if (sqlite3_step(insert.value) != SQLITE_DONE) return Result::Error;
    impl_->volumeKey(state);
    state.blob(2, id.bytes.data(), 32);
    state.counter(3, identity.generation);
    state.blob(4, task.data(), 16);
    if (sqlite3_step(state.value) != SQLITE_DONE) return Result::Error;
    result = transaction.commit();
    if (result == Result::Ready) generation = identity.generation;
    return result;
}
Result LinuxGeocachingInstallJournal::cancel(const TaskId& task, uint64_t generation)
{
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    const auto loaded = impl_->entry(task, value, current);
    if (loaded != Result::Ready) return loaded;
    if (!value.active || value.identity.generation != generation) return Result::Stale;
    value.continue_intent = false;
    const auto result = impl_->write(value);
    return result == Result::Ready ? transaction.commit() : result;
}
Result LinuxGeocachingInstallJournal::prepare(const TaskId& task, uint64_t generation, const Hash& new_hash, gc::ByteView old_hash)
{
    if (!impl_->db) return Result::Error;
    if (old_hash.size && (!old_hash.data || old_hash.size != 32)) return Result::Invalid;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    auto result = impl_->entry(task, value, current);
    if (result != Result::Ready) return result;
    if (!value.active || !value.continue_intent || value.identity.generation != generation) return Result::Stale;
    result = impl_->eligible(value.identity, current);
    if (result != Result::Ready) return result;
    if (current.has_current != (old_hash.size == 32) || (current.has_current && std::memcmp(old_hash.data, current.file_hash.data(), 32))) return Result::Conflict;
    if (value.has_record)
        return value.phase == Phase::Prepared && value.new_file_hash == new_hash && value.has_old_file == current.has_current &&
                       (!value.has_old_file || value.old_file_hash == current.file_hash)
                   ? Result::Ready
                   : Result::Conflict;
    value.has_record = true;
    value.phase = Phase::Prepared;
    value.new_file_hash = new_hash;
    value.has_old_file = current.has_current;
    value.old_file_hash = current.file_hash;
    value.previous_revision = current.hash;
    result = impl_->write(value);
    return result == Result::Ready ? transaction.commit() : result;
}
Result LinuxGeocachingInstallJournal::commit(const TaskId& task, uint64_t generation, const Hash& observed)
{
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    auto result = impl_->entry(task, value, current);
    if (result != Result::Ready) return result;
    if (!value.active || value.identity.generation != generation) return Result::Stale;
    if (!value.has_record || value.new_file_hash != observed) return Result::Conflict;
    if (value.phase == Phase::Installed)
    {
        Records::Record installed;
        result = map(impl_->records->read(value.identity.id, value.identity.hash, installed));
        if (result != Result::Ready) return result == Result::Missing ? Result::Corrupt : result;
        return current.has_current && current.hash.bytes == value.identity.hash.bytes && current.file_hash == observed &&
                       current.highest_seen_revision == installed.summary.revision
                   ? Result::Ready
                   : Result::Corrupt;
    }
    if (!value.continue_intent) return Result::Stale;
    if (value.phase != Phase::Prepared) return Result::Conflict;
    if (value.has_old_file != current.has_current || (current.has_current && (value.old_file_hash != current.file_hash || value.previous_revision.bytes != current.hash.bytes))) return Result::Corrupt;
    uint32_t installed_revision = 0;
    result = impl_->eligible(value.identity, current, &installed_revision);
    if (result != Result::Ready) return result;
    Statement update;
    if (!update.prepare(impl_->db, "UPDATE linux_geocaching_install_state SET installed_hash=?,file_hash=?,highest_seen_revision=? WHERE volume=? AND id=? AND generation=? AND active_task=?")) return Result::Error;
    update.blob(1, value.identity.hash.bytes.data(), 32);
    update.blob(2, observed.data(), 32);
    sqlite3_bind_int64(update.value, 3, installed_revision);
    update.blob(4, impl_->volume.data(), 16);
    update.blob(5, value.identity.id.bytes.data(), 32);
    update.counter(6, generation);
    update.blob(7, task.data(), 16);
    if (sqlite3_step(update.value) != SQLITE_DONE || sqlite3_changes(impl_->db) != 1) return Result::Error;
    value.phase = Phase::Installed;
    value.continue_intent = false;
    result = impl_->write(value);
    return result == Result::Ready ? transaction.commit() : result;
}
Result LinuxGeocachingInstallJournal::finishCleanup(const TaskId& task, uint64_t generation, const Hash& observed)
{
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    auto result = impl_->entry(task, value, current);
    if (result != Result::Ready) return result;
    if (!value.active || value.identity.generation != generation) return Result::Stale;
    if (!value.has_record || value.phase != Phase::Installed || value.new_file_hash != observed || !current.has_current ||
        current.hash.bytes != value.identity.hash.bytes || current.file_hash != observed) return Result::Conflict;
    Records::Record record;
    result = map(impl_->records->read(value.identity.id, value.identity.hash, record));
    if (result != Result::Ready) return result == Result::Missing ? Result::Corrupt : result;
    if (record.summary.revision != current.highest_seen_revision) return Result::Corrupt;
    value.cleanup_complete = true;
    result = impl_->write(value);
    return result == Result::Ready ? transaction.commit() : result;
}
Result LinuxGeocachingInstallJournal::rolledBack(const TaskId& task, uint64_t generation, gc::ByteView observed)
{
    if (!impl_->db) return Result::Error;
    if (observed.size && (!observed.data || observed.size != 32)) return Result::Invalid;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    auto result = impl_->entry(task, value, current);
    if (result != Result::Ready) return result;
    if (!value.active || value.identity.generation != generation) return Result::Stale;
    if (!value.has_record || (value.phase != Phase::Prepared && value.phase != Phase::RolledBack)) return Result::Conflict;
    if (value.has_old_file != (observed.size == 32) || (value.has_old_file && std::memcmp(observed.data, value.old_file_hash.data(), 32)) ||
        current.has_current != value.has_old_file || (current.has_current && current.file_hash != value.old_file_hash)) return Result::Conflict;
    value.phase = Phase::RolledBack;
    value.continue_intent = false;
    result = impl_->write(value);
    return result == Result::Ready ? transaction.commit() : result;
}
Result LinuxGeocachingInstallJournal::markConflict(const TaskId& task, uint64_t generation)
{
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    auto result = impl_->entry(task, value, current);
    if (result != Result::Ready) return result;
    if (!value.active || value.identity.generation != generation) return Result::Stale;
    if (!value.has_record || value.phase == Phase::RolledBack) return Result::Conflict;
    value.phase = Phase::ConflictExternal;
    value.cleanup_complete = false;
    value.continue_intent = false;
    result = impl_->write(value);
    return result == Result::Ready ? transaction.commit() : result;
}
Result LinuxGeocachingInstallJournal::read(const TaskId& task, Entry& out)
{
    out = {};
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, false);
    if (!transaction.active) return Result::Error;
    Impl::State current;
    Entry candidate;
    const auto result = impl_->entry(task, candidate, current);
    if (result == Result::Ready) out = candidate;
    return result;
}
Result LinuxGeocachingInstallJournal::nextRecovery(const TaskId* after, Entry& out)
{
    out = {};
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, false);
    if (!transaction.active) return Result::Error;
    Statement query;
    const auto sql = after ? "SELECT task FROM linux_geocaching_install_tasks WHERE volume=? AND task>? AND record IS NOT NULL ORDER BY task" : "SELECT task FROM linux_geocaching_install_tasks WHERE volume=? AND record IS NOT NULL ORDER BY task";
    if (!query.prepare(impl_->db, sql)) return Result::Error;
    impl_->volumeKey(query);
    if (after) query.blob(2, after->data(), 16);
    int step;
    while ((step = sqlite3_step(query.value)) == SQLITE_ROW)
    {
        TaskId task{};
        if (!copy(query.value, 0, task.data(), 16)) return Result::Corrupt;
        Entry candidate;
        Impl::State current;
        const auto loaded = impl_->entry(task, candidate, current);
        if (loaded != Result::Ready) return loaded;
        if (candidate.phase == Phase::RolledBack || (candidate.phase == Phase::Installed && candidate.cleanup_complete)) continue;
        if (!candidate.active) return Result::Corrupt; // Unresolved work must never be silently superseded.
        out = candidate;
        return Result::Ready;
    }
    return step == SQLITE_DONE ? Result::Missing : Result::Error;
}
Result LinuxGeocachingInstallJournal::installedPage(size_t offset, InstalledPage& out)
{
    out = {};
    if (!impl_->db) return Result::Error;
    if (static_cast<uint64_t>(offset) > static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max())) return Result::Invalid;
    Transaction transaction(impl_->db, false);
    if (!transaction.active) return Result::Error;
    Statement count, rows;
    if (!count.prepare(impl_->db, "SELECT COUNT(*) FROM linux_geocaching_install_state WHERE volume=? AND installed_hash IS NOT NULL")) return Result::Error;
    impl_->volumeKey(count);
    if (sqlite3_step(count.value) != SQLITE_ROW) return Result::Error;
    const auto total = sqlite3_column_int64(count.value, 0);
    if (total < 0 || static_cast<uint64_t>(total) > std::numeric_limits<size_t>::max()) return Result::Corrupt;
    if (!rows.prepare(impl_->db, "SELECT id,installed_hash,file_hash,highest_seen_revision FROM linux_geocaching_install_state WHERE volume=? AND installed_hash IS NOT NULL ORDER BY id LIMIT 4 OFFSET ?")) return Result::Error;
    impl_->volumeKey(rows);
    sqlite3_bind_int64(rows.value, 2, static_cast<sqlite3_int64>(offset));
    InstalledPage page;
    page.offset = offset;
    page.total = static_cast<size_t>(total);
    int step;
    while ((step = sqlite3_step(rows.value)) == SQLITE_ROW)
    {
        auto& row = page.rows[page.count++];
        row.volume = impl_->volume;
        const auto highest = sqlite3_column_int64(rows.value, 3);
        row.valid = copy(rows.value, 0, row.id.bytes.data(), 32) && copy(rows.value, 1, row.hash.bytes.data(), 32) &&
                    copy(rows.value, 2, row.file_hash.data(), 32) && highest > 0 && highest <= UINT32_MAX;
        if (row.valid) row.highest_seen_revision = static_cast<uint32_t>(highest);
    }
    if (step != SQLITE_DONE) return Result::Error;
    out = page;
    return Result::Ready;
}
Result LinuxGeocachingInstallJournal::installedReference(const gc::GeocacheId& id, InstalledReference& out)
{
    out = {};
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, false);
    if (!transaction.active) return Result::Error;
    Statement row;
    if (!row.prepare(impl_->db, "SELECT installed_hash,file_hash,highest_seen_revision FROM linux_geocaching_install_state WHERE volume=? AND id=? AND installed_hash IS NOT NULL")) return Result::Error;
    impl_->volumeKey(row);
    row.blob(2, id.bytes.data(), 32);
    const auto step = sqlite3_step(row.value);
    if (step == SQLITE_DONE) return Result::Missing;
    if (step != SQLITE_ROW) return Result::Error;
    InstalledReference value;
    value.volume = impl_->volume;
    value.id = id;
    const auto highest = sqlite3_column_int64(row.value, 2);
    if (!copy(row.value, 0, value.hash.bytes.data(), 32) || !copy(row.value, 1, value.file_hash.data(), 32) || highest <= 0 || highest > UINT32_MAX) return Result::Corrupt;
    value.valid = true;
    value.highest_seen_revision = static_cast<uint32_t>(highest);
    out = value;
    return Result::Ready;
}
Result LinuxGeocachingInstallJournal::withFileEffect(const TaskId& task, uint64_t generation, FileEffectMode mode, LinuxGeocachingVolumeLease& lease, FileEffect effect, void* context)
{
    if (!impl_->db) return Result::Error;
    if (!effect) return Result::Invalid;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Entry value;
    Impl::State current;
    auto result = impl_->entry(task, value, current);
    if (result != Result::Ready) return result;
    if (!value.active || value.identity.generation != generation) return Result::Stale;
    if (mode == FileEffectMode::Stage)
    {
        if (!value.continue_intent) return Result::Stale;
        if (value.has_record && value.phase != Phase::Prepared) return Result::Conflict;
        result = impl_->eligible(value.identity, current);
        if (result != Result::Ready) return result;
    }
    else if (!value.has_record) return Result::Conflict;
    else if (mode == FileEffectMode::Install)
    {
        if (!value.continue_intent) return Result::Stale;
        if (value.phase != Phase::Prepared) return Result::Conflict;
        result = impl_->eligible(value.identity, current);
        if (result != Result::Ready) return result;
        if (value.has_old_file != current.has_current || (current.has_current && value.old_file_hash != current.file_hash)) return Result::Corrupt;
    }
    else if (mode == FileEffectMode::Rollback)
    {
        if (value.phase != Phase::Prepared) return Result::Conflict;
        if (value.has_old_file != current.has_current || (current.has_current && value.old_file_hash != current.file_hash)) return Result::Corrupt;
    }
    else if (mode == FileEffectMode::Cleanup)
    {
        if (value.phase != Phase::Installed || value.cleanup_complete || !current.has_current ||
            current.hash.bytes != value.identity.hash.bytes || current.file_hash != value.new_file_hash) return Result::Conflict;
    }
    else return Result::Invalid;
    // Recovery must authenticate the exact retained record even if a newer
    // observation now prevents a fresh installation of this revision.
    Records::Record retained;
    result = map(impl_->records->read(value.identity.id, value.identity.hash, retained));
    if (result != Result::Ready) return result == Result::Missing ? Result::Corrupt : result;
    if (value.has_old_file)
    {
        Records::Record old;
        result = map(impl_->records->read(value.identity.id, value.previous_revision, old));
        if (result != Result::Ready) return result == Result::Missing ? Result::Corrupt : result;
    }
    if (mode != FileEffectMode::Install && current.has_current)
    {
        Records::Record previous;
        result = map(impl_->records->read(value.identity.id, current.hash, previous));
        if (result != Result::Ready) return result == Result::Missing ? Result::Corrupt : result;
        if (previous.summary.revision != current.highest_seen_revision) return Result::Corrupt;
    }
    const auto volume_status = [&]
    {
        const auto checked = lease.check(impl_->volume);
        if (checked == LinuxGeocachingVolumeLease::Result::Ready) return Result::Ready;
        if (checked == LinuxGeocachingVolumeLease::Result::Stale || checked == LinuxGeocachingVolumeLease::Result::Mismatch) return Result::Stale;
        return Result::Unavailable;
    };
    result = volume_status();
    if (result != Result::Ready) return result;
    const int root = lease.rootDescriptor();
    if (root < 0) return Result::Unavailable;
    try
    {
        result = effect(context, value, root);
    }
    catch (...)
    {
        return Result::Error;
    }
    if (result != Result::Ready) return result;
    result = volume_status();
    return result == Result::Ready ? transaction.commit() : result;
}
} // namespace platform::linux_runtime
