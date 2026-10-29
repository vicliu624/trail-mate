#include "platform/linux/geocaching_record_store.h"
#include "geocaching/domain/version_policy.h"
#include <algorithm>
#include <array>
#include <limits>
#include <sqlite3.h>

namespace platform::linux_runtime
{
namespace gc = ::geocaching;
namespace
{
using Result = LinuxGeocachingRecordStore::Result;
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement()
    {
        if (value) sqlite3_finalize(value);
    }
    bool prepare(sqlite3* db, const char* sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
    void blob(int index, const uint8_t* bytes, size_t size) { sqlite3_bind_blob(value, index, bytes, static_cast<int>(size), SQLITE_TRANSIENT); }
};
struct Transaction
{
    sqlite3* db;
    bool active;
    Transaction(sqlite3* database, bool write) : db(database), active(sqlite3_exec(db, write ? "BEGIN IMMEDIATE" : "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK) {}
    ~Transaction()
    {
        if (active) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    Result commit(Result result)
    {
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return Result::Error;
        active = false;
        return result;
    }
};
Result verification(gc::protocol::VerificationResult result, bool stored)
{
    if (result == gc::protocol::VerificationResult::Valid) return Result::Ready;
    if (result == gc::protocol::VerificationResult::CryptoUnavailable) return Result::CryptoUnavailable;
    return stored ? Result::Corrupt : Result::Invalid;
}
} // namespace
struct LinuxGeocachingRecordStore::Impl
{
    Impl(std::filesystem::path value, gc::protocol::RecordCrypto& verifier) : path(std::move(value)), crypto(verifier) {}
    ~Impl()
    {
        if (db) sqlite3_close(db);
    }
    struct Head
    {
        bool exists = false, conflict = false, incomplete = true;
        uint64_t retained_count = 0;
        Hash hash;
    };
    struct History
    {
        uint64_t count = 0;
        uint32_t highest = 0, lowest = UINT32_MAX, head_revision = 0;
        bool duplicate = false, head_found = false, conflict = false, incoming_conflict = false;
        bool incomplete() const { return conflict || lowest != 1 || count != highest; }
    };
    Result scan(const Id& id, const Head& head, const gc::protocol::VerifiedRecordView* incoming, History& out)
    {
        out = {};
        Statement history;
        if (!history.prepare(db, "SELECT hash,revision,signed_record FROM linux_geocaching_records WHERE id=? ORDER BY revision,hash")) return Result::Error;
        history.blob(1, id.bytes.data(), 32);
        // Retain version metadata only. SQLite owns each current record blob;
        // no borrowed row pointer survives sqlite3_step().
        gc::protocol::VerifiedRecordView previous;
        bool have_previous = false;
        int step;
        while ((step = sqlite3_step(history.value)) == SQLITE_ROW)
        {
            const auto* hash_bytes = static_cast<const uint8_t*>(sqlite3_column_blob(history.value, 0));
            const auto* stored = static_cast<const uint8_t*>(sqlite3_column_blob(history.value, 2));
            const int size = sqlite3_column_bytes(history.value, 2);
            if (!hash_bytes || sqlite3_column_bytes(history.value, 0) != 32 || !stored || size <= 0 || size > 4166) return Result::Corrupt;
            Hash stored_hash;
            std::copy_n(hash_bytes, 32, stored_hash.bytes.data());
            gc::protocol::VerifiedRecordView known;
            const auto checked = decode(id, stored_hash, {stored, static_cast<size_t>(size)}, known);
            if (checked != Result::Ready) return checked;
            if (sqlite3_column_int64(history.value, 1) != known.record.revision) return Result::Corrupt;
            ++out.count;
            out.highest = std::max(out.highest, known.record.revision);
            out.lowest = std::min(out.lowest, known.record.revision);
            if (incoming && stored_hash.bytes == incoming->hash.bytes) out.duplicate = true;
            if (head.exists && stored_hash.bytes == head.hash.bytes)
            {
                out.head_found = true;
                out.head_revision = known.record.revision;
            }
            if (have_previous && gc::compareGeocacheVersions(previous, known) == gc::VersionRelation::Conflict) out.conflict = true;
            previous.id = known.id;
            previous.hash = known.hash;
            previous.record.revision = known.record.revision;
            previous.record.state = known.record.state;
            previous.record.created_at = known.record.created_at;
            have_previous = true;
            if (incoming && gc::compareGeocacheVersions(known, *incoming) == gc::VersionRelation::Conflict) out.incoming_conflict = true;
        }
        if (step != SQLITE_DONE) return Result::Error;
        if (head.exists != (out.count != 0) || (head.exists && !out.head_found) ||
            (!head.conflict && head.exists && out.head_revision != out.highest)) return Result::Corrupt;
        if (head.exists && head.retained_count != out.count) return Result::Corrupt;
        if (head.exists && head.incomplete != (head.conflict || out.incomplete())) return Result::Corrupt;
        return Result::Ready;
    }
    Result getHead(const Id& id, Head& out)
    {
        out = {};
        Statement query;
        if (!query.prepare(db, "SELECT hash,conflicted,incomplete,retained_count FROM linux_geocaching_heads WHERE id=?")) return Result::Error;
        query.blob(1, id.bytes.data(), 32);
        const auto result = sqlite3_step(query.value);
        if (result == SQLITE_DONE) return Result::Ready;
        if (result != SQLITE_ROW) return Result::Error;
        const auto* hash = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 0));
        if (!hash || sqlite3_column_bytes(query.value, 0) != 32) return Result::Corrupt;
        std::copy_n(hash, 32, out.hash.bytes.data());
        const auto conflict = sqlite3_column_int(query.value, 1), incomplete = sqlite3_column_int(query.value, 2);
        const auto retained_count = sqlite3_column_int64(query.value, 3);
        if ((conflict != 0 && conflict != 1) || (incomplete != 0 && incomplete != 1) || retained_count <= 0) return Result::Corrupt;
        out.exists = true;
        out.conflict = conflict;
        out.incomplete = incomplete;
        out.retained_count = static_cast<uint64_t>(retained_count);
        return Result::Ready;
    }
    Result decode(const Id& id, const Hash& hash, gc::ByteView bytes, gc::protocol::VerifiedRecordView& out)
    {
        return verification(gc::protocol::verifyGeocache(bytes, crypto, scratch.data(), scratch.size(), out, &id, &hash), true);
    }
    Result getRecord(const Id& id, const Hash& hash, Record& out)
    {
        out = {};
        Statement query;
        if (!query.prepare(db, "SELECT revision,signed_record FROM linux_geocaching_records WHERE id=? AND hash=?")) return Result::Error;
        query.blob(1, id.bytes.data(), 32);
        query.blob(2, hash.bytes.data(), 32);
        const auto result = sqlite3_step(query.value);
        if (result == SQLITE_DONE) return Result::Missing;
        if (result != SQLITE_ROW) return Result::Error;
        const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 1));
        const int size = sqlite3_column_bytes(query.value, 1);
        if (!bytes || size <= 0 || size > 4166) return Result::Corrupt;
        gc::protocol::VerifiedRecordView verified;
        const auto checked = decode(id, hash, {bytes, static_cast<size_t>(size)}, verified);
        if (checked != Result::Ready) return checked;
        if (sqlite3_column_int64(query.value, 0) != verified.record.revision) return Result::Corrupt;
        Record candidate;
        candidate.summary.id = id.bytes;
        candidate.summary.hash = hash.bytes;
        candidate.summary.revision = verified.record.revision;
        std::copy(verified.record.name.begin(), verified.record.name.end(), candidate.summary.name.begin());
        candidate.summary.latitude_e7 = verified.record.latitude_e7;
        candidate.summary.longitude_e7 = verified.record.longitude_e7;
        candidate.state = verified.record.state;
        candidate.bytes.assign(bytes, bytes + size);
        out = std::move(candidate);
        return Result::Ready;
    }
    std::filesystem::path path;
    gc::protocol::RecordCrypto& crypto;
    sqlite3* db = nullptr;
    std::array<uint8_t, 4200> scratch{}; // One owner; never borrowed by returned records.
};
LinuxGeocachingRecordStore::LinuxGeocachingRecordStore(std::filesystem::path database, gc::protocol::RecordCrypto& crypto) : impl_(std::make_unique<Impl>(std::move(database), crypto)) {}
LinuxGeocachingRecordStore::~LinuxGeocachingRecordStore() = default;
Result LinuxGeocachingRecordStore::open()
{
    if (impl_->db) return Result::Ready;
    std::error_code error;
    if (!impl_->path.parent_path().empty()) std::filesystem::create_directories(impl_->path.parent_path(), error);
    if (error) return Result::Error;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(impl_->path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return Result::Error;
    }
    sqlite3_busy_timeout(db, 2000);
    const char* schema =
        "PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON; BEGIN IMMEDIATE;"
        "CREATE TABLE IF NOT EXISTS linux_geocaching_records(id BLOB NOT NULL CHECK(length(id)=32),hash BLOB NOT NULL CHECK(length(hash)=32),"
        "revision INTEGER NOT NULL CHECK(revision BETWEEN 1 AND 4294967295),signed_record BLOB NOT NULL CHECK(length(signed_record) BETWEEN 1 AND 4166),"
        "PRIMARY KEY(id,hash)) WITHOUT ROWID;"
        "CREATE INDEX IF NOT EXISTS linux_geocaching_record_versions ON linux_geocaching_records(id,revision);"
        "CREATE TABLE IF NOT EXISTS linux_geocaching_heads(id BLOB PRIMARY KEY CHECK(length(id)=32),hash BLOB NOT NULL CHECK(length(hash)=32),"
        "conflicted INTEGER NOT NULL CHECK(conflicted IN(0,1)),incomplete INTEGER NOT NULL CHECK(incomplete IN(0,1)),retained_count INTEGER NOT NULL CHECK(retained_count>0),"
        "FOREIGN KEY(id,hash) REFERENCES linux_geocaching_records(id,hash)) WITHOUT ROWID; COMMIT;";
    if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return Result::Error;
    }
    impl_->db = db;
    return Result::Ready;
}
Result LinuxGeocachingRecordStore::accept(const Id& id, const Hash& hash, gc::ByteView bytes)
{
    if (!impl_->db) return Result::Error;
    gc::protocol::VerifiedRecordView incoming;
    const auto authenticated = verification(gc::protocol::verifyGeocache(bytes, impl_->crypto, impl_->scratch.data(), impl_->scratch.size(), incoming, &id, &hash), false);
    if (authenticated != Result::Ready) return authenticated;
    Transaction transaction(impl_->db, true);
    if (!transaction.active) return Result::Error;
    Impl::Head head;
    const auto loaded = impl_->getHead(id, head);
    if (loaded != Result::Ready) return loaded;
    Impl::History history;
    const auto scanned = impl_->scan(id, head, &incoming, history);
    if (scanned != Result::Ready) return scanned;
    const bool conflict = head.conflict || history.conflict || history.incoming_conflict;
    if (!history.duplicate)
    {
        Statement insert;
        if (!insert.prepare(impl_->db, "INSERT INTO linux_geocaching_records VALUES(?,?,?,?)")) return Result::Error;
        insert.blob(1, id.bytes.data(), 32);
        insert.blob(2, hash.bytes.data(), 32);
        sqlite3_bind_int64(insert.value, 3, incoming.record.revision);
        insert.blob(4, bytes.data, bytes.size);
        if (sqlite3_step(insert.value) != SQLITE_DONE) return Result::Error;
        ++history.count;
        history.highest = std::max(history.highest, incoming.record.revision);
        history.lowest = std::min(history.lowest, incoming.record.revision);
    }
    const bool advance = !head.exists || (!conflict && incoming.record.revision > history.head_revision);
    const auto& selected = advance ? hash : head.hash;
    const bool incomplete = conflict || history.incomplete();
    Statement update;
    if (history.count > static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max())) return Result::Error;
    if (!update.prepare(impl_->db, "INSERT INTO linux_geocaching_heads VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET hash=excluded.hash,conflicted=excluded.conflicted,incomplete=excluded.incomplete,retained_count=excluded.retained_count")) return Result::Error;
    update.blob(1, id.bytes.data(), 32);
    update.blob(2, selected.bytes.data(), 32);
    sqlite3_bind_int(update.value, 3, conflict);
    sqlite3_bind_int(update.value, 4, incomplete);
    sqlite3_bind_int64(update.value, 5, static_cast<sqlite3_int64>(history.count));
    if (sqlite3_step(update.value) != SQLITE_DONE) return Result::Error;
    const auto result = conflict ? Result::Conflict : advance                                        ? Result::Ready
                                                  : incoming.record.revision < history.head_revision ? Result::Older
                                                                                                     : Result::Identical;
    return transaction.commit(result);
}
Result LinuxGeocachingRecordStore::head(const Id& id, Record& out)
{
    out = {};
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db, false);
    if (!transaction.active) return Result::Error;
    Impl::Head head;
    const auto loaded = impl_->getHead(id, head);
    if (loaded != Result::Ready) return loaded;
    Impl::History history;
    const auto scanned = impl_->scan(id, head, nullptr, history);
    if (scanned != Result::Ready) return scanned;
    if (!head.exists) return Result::Missing;
    if (head.conflict || history.conflict) return Result::Conflict;
    if (head.incomplete != history.incomplete()) return Result::Corrupt;
    const auto result = impl_->getRecord(id, head.hash, out);
    if (result == Result::Missing) return Result::Corrupt;
    if (result == Result::Ready) out.history_incomplete = head.incomplete;
    return result;
}
Result LinuxGeocachingRecordStore::read(const Id& id, const Hash& hash, Record& out)
{
    out = {};
    return impl_->db ? impl_->getRecord(id, hash, out) : Result::Error;
}
} // namespace platform::linux_runtime
