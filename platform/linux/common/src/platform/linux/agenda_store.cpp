#include "platform/linux/agenda_store.h"
#if defined(__linux__)
#include "agenda/domain/event_codec.h"
#include <cstring>
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace platform::linux_runtime
{
namespace
{
using Result = ::agenda::StoreResult;
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement() { sqlite3_finalize(value); }
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
    bool commit()
    {
        if (!active || sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        active = false;
        return true;
    }
};
Result decode(const uint8_t (&bytes)[::agenda::kSlotBytes], ::agenda::EventRecord& out)
{
    switch (::agenda::decodeEvent(bytes, out))
    {
    case ::agenda::DecodeResult::Ok:
        return Result::Ok;
    case ::agenda::DecodeResult::UnsupportedVersion:
        return Result::UnsupportedVersion;
    case ::agenda::DecodeResult::InvalidRecord:
        return Result::InvalidRecord;
    default:
        return Result::Corrupt;
    }
}
} // namespace
struct LinuxAgendaStore::Impl
{
    explicit Impl(std::filesystem::path value) : path(std::move(value)) {}
    ~Impl() { close(); }
    void close()
    {
        ready = false;
        if (db) sqlite3_close(db);
        db = nullptr;
        if (owner >= 0) ::close(owner);
        owner = -1;
    }
    bool execute(const char* sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
    Result header()
    {
        Statement query;
        if (!query.prepare(db, "SELECT id,version,slot_count FROM linux_agenda_meta")) return Result::Corrupt;
        const auto step = sqlite3_step(query.value);
        if (step != SQLITE_ROW) return step == SQLITE_DONE ? Result::Corrupt : Result::IoError;
        for (int column = 0; column < 3; ++column)
            if (sqlite3_column_type(query.value, column) != SQLITE_INTEGER) return Result::Corrupt;
        if (sqlite3_column_int64(query.value, 0) != 1) return Result::Corrupt;
        if (sqlite3_column_int64(query.value, 1) != 1 || sqlite3_column_int64(query.value, 2) != ::agenda::kMaxActiveEvents) return Result::UnsupportedVersion;
        return sqlite3_step(query.value) == SQLITE_DONE ? Result::Ok : Result::Corrupt;
    }
    Result initialize()
    {
        Transaction transaction(db);
        if (!transaction.active || !execute(
                                       "CREATE TABLE linux_agenda_meta(id INTEGER PRIMARY KEY CHECK(id=1),version INTEGER NOT NULL,slot_count INTEGER NOT NULL);"
                                       "CREATE TABLE linux_agenda_slots(slot INTEGER PRIMARY KEY,record BLOB NOT NULL);")) return Result::IoError;
        Statement meta, write;
        if (!meta.prepare(db, "INSERT INTO linux_agenda_meta VALUES(1,1,?1)") ||
            sqlite3_bind_int(meta.value, 1, ::agenda::kMaxActiveEvents) != SQLITE_OK || sqlite3_step(meta.value) != SQLITE_DONE ||
            !write.prepare(db, "INSERT INTO linux_agenda_slots VALUES(?1,?2)")) return Result::IoError;
        ::agenda::EventRecord empty;
        if (!::agenda::encodeEvent(empty, bytes)) return Result::InvalidRecord;
        for (uint16_t slot = 0; slot < ::agenda::kMaxActiveEvents; ++slot)
            if (sqlite3_bind_int(write.value, 1, slot) != SQLITE_OK || sqlite3_bind_blob(write.value, 2, bytes, sizeof(bytes), SQLITE_STATIC) != SQLITE_OK ||
                sqlite3_step(write.value) != SQLITE_DONE || sqlite3_reset(write.value) != SQLITE_OK) return Result::IoError;
        return transaction.commit() ? Result::Ok : Result::IoError;
    }
    Result read(uint16_t slot, ::agenda::EventRecord& out)
    {
        out = {};
        Statement query;
        if (!query.prepare(db, "SELECT record FROM linux_agenda_slots WHERE slot=?1") || sqlite3_bind_int(query.value, 1, slot) != SQLITE_OK) return Result::IoError;
        const auto step = sqlite3_step(query.value);
        if (step != SQLITE_ROW) return step == SQLITE_DONE ? Result::Corrupt : Result::IoError;
        if (sqlite3_column_type(query.value, 0) != SQLITE_BLOB || sqlite3_column_bytes(query.value, 0) != static_cast<int>(sizeof(bytes))) return Result::Corrupt;
        std::memcpy(bytes, sqlite3_column_blob(query.value, 0), sizeof(bytes));
        const auto result = decode(bytes, out);
        if (result != Result::Ok) out = {};
        return result;
    }
    std::filesystem::path path;
    sqlite3* db = nullptr;
    int owner = -1;
    bool ready = false;
    uint8_t bytes[::agenda::kSlotBytes]{};
};
LinuxAgendaStore::LinuxAgendaStore(std::filesystem::path database) : impl_(std::make_unique<Impl>(std::move(database))) {}
LinuxAgendaStore::~LinuxAgendaStore() = default;
::agenda::StoreResult LinuxAgendaStore::begin()
{
    if (impl_->ready) return impl_->header();
    impl_->close();
    std::error_code error;
    if (!impl_->path.parent_path().empty()) std::filesystem::create_directories(impl_->path.parent_path(), error);
    if (error || impl_->path.empty()) return Result::IoError;
    const auto lock_path = impl_->path.string() + ".agenda-owner";
    impl_->owner = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat status
    {
    };
    if (impl_->owner < 0 || ::fstat(impl_->owner, &status) != 0 || !S_ISREG(status.st_mode) || ::flock(impl_->owner, LOCK_EX | LOCK_NB) != 0)
    {
        impl_->close();
        return Result::IoError;
    }
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    if (sqlite3_open_v2(impl_->path.c_str(), &impl_->db, flags, nullptr) != SQLITE_OK ||
        sqlite3_busy_timeout(impl_->db, 250) != SQLITE_OK || !impl_->execute("PRAGMA synchronous=FULL"))
    {
        impl_->close();
        return Result::IoError;
    }
    Statement tables;
    if (!tables.prepare(impl_->db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name IN ('linux_agenda_meta','linux_agenda_slots')") || sqlite3_step(tables.value) != SQLITE_ROW) return Result::IoError;
    const auto count = sqlite3_column_int(tables.value, 0);
    if (sqlite3_reset(tables.value) != SQLITE_OK) return Result::IoError;
    if (!count)
    {
        const auto created = impl_->initialize();
        if (created != Result::Ok) return created;
    }
    else if (count != 2) return Result::Corrupt;
    const auto header = impl_->header();
    if (header != Result::Ok) return header;
    Statement slots;
    if (!slots.prepare(impl_->db, "SELECT slot FROM linux_agenda_slots ORDER BY slot")) return Result::Corrupt;
    uint16_t expected = 0;
    int step;
    while ((step = sqlite3_step(slots.value)) == SQLITE_ROW)
    {
        if (expected >= slotCount() || sqlite3_column_type(slots.value, 0) != SQLITE_INTEGER || sqlite3_column_int64(slots.value, 0) != expected) return Result::Corrupt;
        ++expected;
    }
    if (step != SQLITE_DONE) return Result::IoError;
    if (expected != slotCount()) return Result::Corrupt;
    impl_->ready = true;
    return Result::Ok;
}
::agenda::StoreResult LinuxAgendaStore::readSlot(uint16_t slot, ::agenda::EventRecord& out)
{
    out = {};
    if (slot >= slotCount()) return Result::OutOfRange;
    if (!impl_->ready) return Result::IoError;
    const auto header = impl_->header();
    return header == Result::Ok ? impl_->read(slot, out) : header;
}
::agenda::StoreResult LinuxAgendaStore::writeSlot(uint16_t slot, const ::agenda::EventRecord& record)
{
    if (slot >= slotCount()) return Result::OutOfRange;
    if (!::agenda::validEvent(record)) return Result::InvalidRecord;
    if (!impl_->ready) return Result::IoError;
    Transaction transaction(impl_->db);
    if (!transaction.active) return Result::IoError;
    const auto header = impl_->header();
    if (header != Result::Ok) return header;
    ::agenda::EventRecord previous;
    const auto valid = impl_->read(slot, previous);
    if (valid != Result::Ok) return valid;
    if (!::agenda::encodeEvent(record, impl_->bytes)) return Result::InvalidRecord;
    Statement write;
    if (!write.prepare(impl_->db, "UPDATE linux_agenda_slots SET record=?1 WHERE slot=?2") ||
        sqlite3_bind_blob(write.value, 1, impl_->bytes, sizeof(impl_->bytes), SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_int(write.value, 2, slot) != SQLITE_OK || sqlite3_step(write.value) != SQLITE_DONE) return Result::IoError;
    if (sqlite3_changes(impl_->db) != 1) return Result::Corrupt;
    return transaction.commit() ? Result::Ok : Result::IoError;
}
::agenda::StoreResult LinuxAgendaStore::eraseSlot(uint16_t slot)
{
    ::agenda::EventRecord record;
    const auto result = readSlot(slot, record);
    if (result != Result::Ok) return result;
    const auto id = record.id;
    record = {};
    record.id = id;
    record.state = ::agenda::RecordState::Deleted;
    return writeSlot(slot, record);
}
} // namespace platform::linux_runtime
#else
namespace platform::linux_runtime
{
struct LinuxAgendaStore::Impl
{
};
LinuxAgendaStore::LinuxAgendaStore(std::filesystem::path) : impl_(std::make_unique<Impl>()) {}
LinuxAgendaStore::~LinuxAgendaStore() = default;
::agenda::StoreResult LinuxAgendaStore::begin() { return ::agenda::StoreResult::IoError; }
::agenda::StoreResult LinuxAgendaStore::readSlot(uint16_t, ::agenda::EventRecord& out)
{
    out = {};
    return ::agenda::StoreResult::IoError;
}
::agenda::StoreResult LinuxAgendaStore::writeSlot(uint16_t, const ::agenda::EventRecord&) { return ::agenda::StoreResult::IoError; }
::agenda::StoreResult LinuxAgendaStore::eraseSlot(uint16_t) { return ::agenda::StoreResult::IoError; }
} // namespace platform::linux_runtime
#endif
