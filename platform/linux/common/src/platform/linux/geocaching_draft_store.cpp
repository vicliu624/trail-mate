#include "platform/linux/geocaching_draft_store.h"
#include "geocaching/storage/draft_edit.h"
#include "platform/linux/geocaching_crypto.h"
#include <algorithm>
#include <limits>
#include <sqlite3.h>

namespace platform::linux_runtime
{
namespace
{
using Result = LinuxGeocachingDraftStore::Result;
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement()
    {
        if (value) sqlite3_finalize(value);
    }
    bool prepare(sqlite3* db, const char* sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
};
struct Transaction
{
    sqlite3* db;
    bool active;
    explicit Transaction(sqlite3* value) : db(value), active(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK) {}
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
bool validId(const LinuxGeocachingDraftStore::Id& id)
{
    return std::any_of(id.begin(), id.end(), [](uint8_t value)
                       { return value != 0; });
}
} // namespace
struct LinuxGeocachingDraftStore::Impl
{
    std::filesystem::path path;
    sqlite3* db = nullptr;
    explicit Impl(std::filesystem::path value) : path(std::move(value)) {}
    ~Impl()
    {
        if (db) sqlite3_close(db);
    }
    Result row(const Id& id, Row& out, bool& deleted)
    {
        out = {};
        deleted = false;
        if (!db) return Result::Error;
        Statement query;
        if (!query.prepare(db, "SELECT encoded,deleted FROM linux_geocaching_drafts WHERE id=?")) return Result::Error;
        sqlite3_bind_blob(query.value, 1, id.data(), id.size(), SQLITE_TRANSIENT);
        const auto step = sqlite3_step(query.value);
        if (step == SQLITE_DONE) return Result::Missing;
        if (step != SQLITE_ROW) return Result::Error;
        const auto size = sqlite3_column_bytes(query.value, 0);
        const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 0));
        if (!bytes || size <= 0 || size > 32768) return Result::Corrupt;
        out.id = id;
        out.bytes.assign(bytes, bytes + size);
        ::geocaching::storage::DraftView draft;
        if (!out.view(draft))
        {
            out = {};
            return Result::Corrupt;
        }
        deleted = sqlite3_column_int(query.value, 1) != 0;
        return Result::Ready;
    }
    Result save(const Id& id, uint64_t expected, ::geocaching::ByteView editor, const uint8_t* author = nullptr)
    {
        using namespace ::geocaching::storage;
        DraftView incoming;
        if (!validId(id) || !decodeDraft({id.data(), id.size()}, editor, incoming) || incoming.author.size || incoming.base_hash.size ||
            expected == UINT64_MAX || incoming.generation != expected + 1) return Result::Invalid;
        Row previous;
        bool deleted = false;
        const auto loaded = row(id, previous, deleted);
        if (loaded != Result::Ready && loaded != Result::Missing) return loaded;
        if (deleted) return Result::Conflict;
        DraftView prior;
        const bool exists = loaded == Result::Ready;
        if (exists && !previous.view(prior)) return Result::Corrupt;
        if (author && prior.author.size && std::memcmp(prior.author.data, author, 64)) return Result::Forbidden;
        std::vector<uint8_t> bytes(editor.size + 101);
        std::copy_n(editor.data, editor.size, bytes.data());
        size_t size = editor.size;
        if (!restoreDraftIdentity({id.data(), id.size()}, exists ? &prior : nullptr, expected, bytes.data(), bytes.size(), size)) return Result::Conflict;
        DraftView next;
        if (!decodeDraft({id.data(), id.size()}, {bytes.data(), size}, next)) return Result::Invalid;
        switch (checkDraftUpdate(exists ? &prior : nullptr, next, expected))
        {
        case DraftUpdateCheck::Allowed:
            break;
        case DraftUpdateCheck::NeedsRetainedPublication:
            return Result::PublicationRequired;
        default:
            return Result::Conflict;
        }
        Statement write;
        if (!write.prepare(db, "INSERT INTO linux_geocaching_drafts(id,encoded) VALUES(?,?) ON CONFLICT(id) DO UPDATE SET encoded=excluded.encoded")) return Result::Error;
        sqlite3_bind_blob(write.value, 1, id.data(), id.size(), SQLITE_TRANSIENT);
        sqlite3_bind_blob(write.value, 2, bytes.data(), size, SQLITE_TRANSIENT);
        return sqlite3_step(write.value) == SQLITE_DONE ? Result::Ready : Result::Error;
    }
};
bool LinuxGeocachingDraftStore::Row::view(::geocaching::storage::DraftView& out) const
{
    return ::geocaching::storage::decodeDraft({id.data(), id.size()}, {bytes.data(), bytes.size()}, out);
}
LinuxGeocachingDraftStore::LinuxGeocachingDraftStore(std::filesystem::path database) : impl_(std::make_unique<Impl>(std::move(database))) {}
LinuxGeocachingDraftStore::~LinuxGeocachingDraftStore() = default;
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::open()
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
    if (sqlite3_exec(db, "PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS linux_geocaching_drafts("
                         "id BLOB PRIMARY KEY CHECK(length(id)=16),encoded BLOB NOT NULL CHECK(length(encoded) BETWEEN 1 AND 32768),"
                         "deleted INTEGER NOT NULL DEFAULT 0 CHECK(deleted IN(0,1))) WITHOUT ROWID;"
                         "CREATE TABLE IF NOT EXISTS linux_geocaching_draft_catalog(singleton INTEGER PRIMARY KEY CHECK(singleton=1),generation INTEGER NOT NULL CHECK(typeof(generation)='integer' AND generation>0));"
                         "INSERT OR IGNORE INTO linux_geocaching_draft_catalog VALUES(1,1);"
                         "CREATE TRIGGER IF NOT EXISTS linux_geocaching_draft_insert AFTER INSERT ON linux_geocaching_drafts BEGIN UPDATE linux_geocaching_draft_catalog SET generation=generation+1 WHERE singleton=1; END;"
                         "CREATE TRIGGER IF NOT EXISTS linux_geocaching_draft_update AFTER UPDATE ON linux_geocaching_drafts BEGIN UPDATE linux_geocaching_draft_catalog SET generation=generation+1 WHERE singleton=1; END;"
                         "CREATE TRIGGER IF NOT EXISTS linux_geocaching_draft_delete AFTER DELETE ON linux_geocaching_drafts BEGIN UPDATE linux_geocaching_draft_catalog SET generation=generation+1 WHERE singleton=1; END;"
                         "CREATE TABLE IF NOT EXISTS linux_geocaching_web_draft_receipts(draft BLOB PRIMARY KEY CHECK(length(draft)=16),transfer BLOB UNIQUE NOT NULL CHECK(length(transfer)=16),"
                         "expected BLOB NOT NULL CHECK(length(expected)=8),author BLOB NOT NULL CHECK(length(author)=64),generation INTEGER NOT NULL,hash BLOB NOT NULL CHECK(length(hash)=32)) WITHOUT ROWID",
                     nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        return Result::Error;
    }
    impl_->db = db;
    return Result::Ready;
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::read(const Id& id, Row& out)
{
    bool deleted = false;
    const auto result = impl_->row(id, out, deleted);
    if (deleted)
    {
        out = {};
        return Result::Missing;
    }
    return result;
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::save(const Id& id, uint64_t expected, ::geocaching::ByteView editor)
{
    ::geocaching::storage::DraftView incoming;
    if (!validId(id) || !::geocaching::storage::decodeDraft({id.data(), id.size()}, editor, incoming) || incoming.author.size || incoming.base_hash.size ||
        expected == UINT64_MAX || incoming.generation != expected + 1) return Result::Invalid;
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db);
    if (!transaction.active) return Result::Error;
    const auto saved = impl_->save(id, expected, editor);
    return saved == Result::Ready ? transaction.commit() : saved;
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::erase(const Id& id, uint64_t expected)
{
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db);
    if (!transaction.active) return Result::Error;
    Row previous;
    bool deleted = false;
    auto result = impl_->row(id, previous, deleted);
    if (result != Result::Ready) return result;
    ::geocaching::storage::DraftView draft;
    if (!previous.view(draft)) return Result::Corrupt;
    if (deleted || draft.generation != expected) return Result::Conflict;
    Statement write;
    if (!write.prepare(impl_->db, "UPDATE linux_geocaching_drafts SET deleted=1 WHERE id=?")) return Result::Error;
    sqlite3_bind_blob(write.value, 1, id.data(), id.size(), SQLITE_TRANSIENT);
    if (sqlite3_step(write.value) != SQLITE_DONE) return Result::Error;
    return transaction.commit();
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::saveWeb(const Id& id, uint64_t expected, ::geocaching::ByteView editor,
                                                                     const uint8_t author[64], uint32_t generation, const Id& transfer, const uint8_t hash[32])
{
    ::geocaching::storage::DraftView input;
    if (!validId(id) || !validId(transfer) || !author || !generation || !hash || expected == UINT64_MAX ||
        !::geocaching::storage::decodeDraft({id.data(), 16}, editor, input) || input.author.size || input.base_hash.size || input.generation != expected + 1) return Result::Invalid;
    std::array<uint8_t, 32> actual{};
    LinuxGeocachingCrypto crypto;
    if (!crypto.sha256(editor, actual.data())) return Result::Error;
    if (std::memcmp(actual.data(), hash, 32)) return Result::Invalid;
    if (!impl_->db) return Result::Error;
    Transaction transaction(impl_->db);
    if (!transaction.active) return Result::Error;
    std::array<uint8_t, 8> previous{};
    for (unsigned i = 0; i < 8; ++i) previous[i] = uint8_t(expected >> (8 * i));
    Statement receipt;
    if (!receipt.prepare(impl_->db, "SELECT draft,expected,author,generation,hash FROM linux_geocaching_web_draft_receipts WHERE transfer=?")) return Result::Error;
    sqlite3_bind_blob(receipt.value, 1, transfer.data(), 16, SQLITE_TRANSIENT);
    const auto step = sqlite3_step(receipt.value);
    if (step == SQLITE_ROW)
    {
        const auto matches = [&](int column, const uint8_t* bytes, int size)
        { return sqlite3_column_bytes(receipt.value, column) == size && sqlite3_column_blob(receipt.value, column) && !std::memcmp(sqlite3_column_blob(receipt.value, column), bytes, size); };
        if (!matches(0, id.data(), 16) || !matches(1, previous.data(), 8) || !matches(2, author, 64) || sqlite3_column_int64(receipt.value, 3) != generation || !matches(4, hash, 32)) return Result::Conflict;
        Row row;
        bool deleted = false;
        const auto loaded = impl_->row(id, row, deleted);
        ::geocaching::storage::DraftView current;
        if (loaded != Result::Ready) return loaded == Result::Missing ? Result::Conflict : loaded;
        if (deleted || !row.view(current) || current.generation != expected + 1 || (current.author.size && std::memcmp(current.author.data, author, 64))) return Result::Conflict;
        current.author = {};
        current.base_hash = {};
        std::vector<uint8_t> canonical(row.bytes.size());
        size_t bytes = 0;
        if (!::geocaching::storage::encodeDraft({id.data(), 16}, current, canonical.data(), canonical.size(), bytes) || bytes != editor.size || std::memcmp(canonical.data(), editor.data, bytes)) return Result::Conflict;
        return transaction.commit();
    }
    if (step != SQLITE_DONE) return Result::Error;
    const auto saved = impl_->save(id, expected, editor, author);
    if (saved != Result::Ready) return saved;
    Statement write;
    if (!write.prepare(impl_->db, "INSERT INTO linux_geocaching_web_draft_receipts(draft,transfer,expected,author,generation,hash) VALUES(?,?,?,?,?,?) "
                                  "ON CONFLICT(draft) DO UPDATE SET transfer=excluded.transfer,expected=excluded.expected,author=excluded.author,generation=excluded.generation,hash=excluded.hash")) return Result::Error;
    sqlite3_bind_blob(write.value, 1, id.data(), 16, SQLITE_TRANSIENT);
    sqlite3_bind_blob(write.value, 2, transfer.data(), 16, SQLITE_TRANSIENT);
    sqlite3_bind_blob(write.value, 3, previous.data(), 8, SQLITE_TRANSIENT);
    sqlite3_bind_blob(write.value, 4, author, 64, SQLITE_TRANSIENT);
    sqlite3_bind_int64(write.value, 5, generation);
    sqlite3_bind_blob(write.value, 6, hash, 32, SQLITE_TRANSIENT);
    if (sqlite3_step(write.value) != SQLITE_DONE) return Result::Error;
    return transaction.commit();
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::catalogGeneration(uint64_t& out)
{
    Statement generation;
    if (!impl_->db || !generation.prepare(impl_->db, "SELECT generation FROM linux_geocaching_draft_catalog WHERE singleton=1") || sqlite3_step(generation.value) != SQLITE_ROW ||
        sqlite3_column_type(generation.value, 0) != SQLITE_INTEGER || sqlite3_column_int64(generation.value, 0) <= 0) return Result::Corrupt;
    out = sqlite3_column_int64(generation.value, 0);
    return Result::Ready;
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::catalog(uint32_t offset, bool bound, uint64_t expected, Catalog& out)
{
    out = {};
    if (!impl_->db || (offset && !bound)) return Result::Invalid;
    if (sqlite3_exec(impl_->db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return Result::Error;
    struct End
    {
        sqlite3* db;
        ~End() { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    } end{impl_->db};
    Statement generation, count, row;
    if (!generation.prepare(impl_->db, "SELECT generation FROM linux_geocaching_draft_catalog WHERE singleton=1") || sqlite3_step(generation.value) != SQLITE_ROW ||
        sqlite3_column_type(generation.value, 0) != SQLITE_INTEGER || sqlite3_column_int64(generation.value, 0) <= 0) return Result::Corrupt;
    out.generation = sqlite3_column_int64(generation.value, 0);
    if (bound && expected != out.generation) return Result::Conflict;
    if (!count.prepare(impl_->db, "SELECT COUNT(*) FROM linux_geocaching_drafts WHERE deleted=0") || sqlite3_step(count.value) != SQLITE_ROW ||
        !row.prepare(impl_->db, "SELECT id FROM linux_geocaching_drafts WHERE deleted=0 ORDER BY id LIMIT 1 OFFSET ?")) return Result::Error;
    out.total = sqlite3_column_int64(count.value, 0);
    sqlite3_bind_int64(row.value, 1, offset);
    const auto step = sqlite3_step(row.value);
    if (step == SQLITE_DONE) return Result::Ready;
    if (step != SQLITE_ROW) return Result::Error;
    if (sqlite3_column_bytes(row.value, 0) != 16 || !sqlite3_column_blob(row.value, 0)) return Result::Corrupt;
    Id id;
    std::memcpy(id.data(), sqlite3_column_blob(row.value, 0), 16);
    bool deleted = false;
    const auto loaded = impl_->row(id, out.row, deleted);
    if (loaded != Result::Ready || deleted) return loaded == Result::Ready ? Result::Corrupt : loaded;
    out.present = true;
    return Result::Ready;
}
LinuxGeocachingDraftStore::Result LinuxGeocachingDraftStore::page(size_t offset, Page& out)
{
    out = {};
    if (!impl_->db || offset > static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max())) return Result::Error;
    // A read transaction keeps count and the bounded projection coherent.
    if (sqlite3_exec(impl_->db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return Result::Error;
    struct ReadEnd
    {
        sqlite3* db;
        ~ReadEnd() { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    } end{impl_->db};
    Statement count, rows;
    if (!count.prepare(impl_->db, "SELECT COUNT(*) FROM linux_geocaching_drafts WHERE deleted=0") || sqlite3_step(count.value) != SQLITE_ROW ||
        !rows.prepare(impl_->db, "SELECT id,encoded FROM linux_geocaching_drafts WHERE deleted=0 ORDER BY id LIMIT 4 OFFSET ?")) return Result::Error;
    Page page;
    page.total = sqlite3_column_int64(count.value, 0);
    sqlite3_bind_int64(rows.value, 1, offset);
    int status;
    while ((status = sqlite3_step(rows.value)) == SQLITE_ROW)
    {
        const auto* id = static_cast<const uint8_t*>(sqlite3_column_blob(rows.value, 0));
        const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(rows.value, 1));
        const auto size = sqlite3_column_bytes(rows.value, 1);
        ::geocaching::storage::DraftView draft;
        if (!id || sqlite3_column_bytes(rows.value, 0) != 16 || size <= 0 || size > 32768 ||
            !::geocaching::storage::decodeDraft({id, 16}, {bytes, static_cast<size_t>(size)}, draft)) return Result::Corrupt;
        auto& summary = page.rows[page.count++];
        std::copy_n(id, 16, summary.id.data());
        std::copy(draft.name.begin(), draft.name.end(), summary.name.begin());
        summary.generation = draft.generation;
        summary.state = draft.state;
        summary.latitude_e7 = draft.latitude_e7;
        summary.longitude_e7 = draft.longitude_e7;
        summary.has_coordinates = draft.has_coordinates;
    }
    if (status != SQLITE_DONE) return Result::Error;
    out = page;
    return Result::Ready;
}
} // namespace platform::linux_runtime
