#include "platform/linux/waypoint_store.h"
#include "waypoint/record_codec.h"
#include <cstring>
#include <limits>
#if defined(__linux__)
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
using ::waypoint::Result;
#if defined(__linux__)
namespace
{
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
} // namespace
struct LinuxWaypointStore::Impl
{
    std::filesystem::path path;
    sqlite3* db = nullptr;
    int owner = -1;
    bool ready = false;
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
    bool exec(const char* sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
    Result header()
    {
        Statement query;
        if (!query.prepare(db, "SELECT id,version,slot_count FROM linux_waypoint_meta")) return Result::Corrupt;
        int step = sqlite3_step(query.value);
        if (step != SQLITE_ROW) return step == SQLITE_DONE ? Result::Corrupt : Result::IoError;
        for (int column = 0; column < 3; ++column)
            if (sqlite3_column_type(query.value, column) != SQLITE_INTEGER) return Result::Corrupt;
        if (sqlite3_column_int64(query.value, 0) != 1 || sqlite3_column_int64(query.value, 1) != 1 ||
            sqlite3_column_int64(query.value, 2) != ::waypoint::kMaxWaypoints) return Result::Corrupt;
        return sqlite3_step(query.value) == SQLITE_DONE ? Result::Ok : Result::Corrupt;
    }
    Result read(uint16_t index, ::waypoint::Slot& slot)
    {
        slot = {};
        Statement query;
        if (!query.prepare(db, "SELECT record FROM linux_waypoint_slots WHERE slot=?1") ||
            sqlite3_bind_int(query.value, 1, index) != SQLITE_OK) return Result::IoError;
        const auto step = sqlite3_step(query.value);
        if (step != SQLITE_ROW) return step == SQLITE_DONE ? Result::Corrupt : Result::IoError;
        if (sqlite3_column_type(query.value, 0) != SQLITE_BLOB || sqlite3_column_bytes(query.value, 0) != ::waypoint::kEncodedBytes ||
            !::waypoint::decode(static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 0)), ::waypoint::kEncodedBytes, slot)) return Result::Corrupt;
        return Result::Ok;
    }
    Result mutate(const ::waypoint::Record& value, bool creating, bool deleting, uint32_t& id)
    {
        id = 0;
        auto validation = value;
        if (creating) validation.id = 1;
        if ((!deleting && !::waypoint::valid(validation)) || (deleting && !value.id)) return Result::Invalid;
        if (!ready) return Result::IoError;
        Transaction transaction(db);
        if (!transaction.active) return Result::IoError;
        auto result = header();
        if (result != Result::Ok) return result;
        uint16_t target = ::waypoint::kMaxWaypoints;
        uint32_t largest = 0;
        ::waypoint::Slot slot;
        for (uint16_t index = 0; index < ::waypoint::kMaxWaypoints; ++index)
        {
            result = read(index, slot);
            if (result != Result::Ok) return result;
            if (slot.record.id > largest) largest = slot.record.id;
            if (creating ? (slot.state != ::waypoint::SlotState::Active && target == ::waypoint::kMaxWaypoints)
                         : (slot.state == ::waypoint::SlotState::Active && slot.record.id == value.id)) target = index;
        }
        if (target == ::waypoint::kMaxWaypoints) return creating ? Result::Full : Result::NotFound;
        if (creating && largest == std::numeric_limits<uint32_t>::max()) return Result::Full;
        result = read(target, slot);
        if (result != Result::Ok) return result;
        if (slot.generation == std::numeric_limits<uint64_t>::max()) return Result::Full;
        ++slot.generation;
        slot.record = value;
        slot.record.id = creating ? largest + 1 : value.id;
        slot.state = deleting ? ::waypoint::SlotState::Deleted : ::waypoint::SlotState::Active;
        uint8_t bytes[::waypoint::kEncodedBytes];
        if (!::waypoint::encode(slot, bytes, sizeof(bytes))) return Result::Invalid;
        Statement write;
        if (!write.prepare(db, "UPDATE linux_waypoint_slots SET record=?1 WHERE slot=?2") ||
            sqlite3_bind_blob(write.value, 1, bytes, sizeof(bytes), SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int(write.value, 2, target) != SQLITE_OK || sqlite3_step(write.value) != SQLITE_DONE ||
            sqlite3_changes(db) != 1 || !transaction.commit()) return Result::IoError;
        id = slot.record.id;
        return Result::Ok;
    }
};
LinuxWaypointStore::LinuxWaypointStore(std::filesystem::path path) : impl_(std::make_unique<Impl>(std::move(path))) {}
LinuxWaypointStore::~LinuxWaypointStore() = default;
Result LinuxWaypointStore::begin()
{
    if (impl_->ready) return impl_->header();
    impl_->close();
    std::error_code error;
    if (!impl_->path.parent_path().empty()) std::filesystem::create_directories(impl_->path.parent_path(), error);
    if (error || impl_->path.empty()) return Result::IoError;
    impl_->owner = ::open((impl_->path.string() + ".waypoint-owner").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
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
        sqlite3_busy_timeout(impl_->db, 250) != SQLITE_OK || !impl_->exec("PRAGMA synchronous=FULL"))
    {
        impl_->close();
        return Result::IoError;
    }
    Statement tables;
    if (!tables.prepare(impl_->db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name IN ('linux_waypoint_meta','linux_waypoint_slots')") ||
        sqlite3_step(tables.value) != SQLITE_ROW) return Result::IoError;
    const auto count = sqlite3_column_int(tables.value, 0);
    if (sqlite3_reset(tables.value) != SQLITE_OK) return Result::IoError;
    if (count == 0)
    {
        Transaction transaction(impl_->db);
        if (!transaction.active || !impl_->exec("CREATE TABLE linux_waypoint_meta(id INTEGER PRIMARY KEY CHECK(id=1),version INTEGER NOT NULL,slot_count INTEGER NOT NULL);"
                                                "CREATE TABLE linux_waypoint_slots(slot INTEGER PRIMARY KEY,record BLOB NOT NULL);")) return Result::IoError;
        Statement insert, meta;
        if (!meta.prepare(impl_->db, "INSERT INTO linux_waypoint_meta VALUES(1,1,?1)") ||
            sqlite3_bind_int(meta.value, 1, slotCount()) != SQLITE_OK || sqlite3_step(meta.value) != SQLITE_DONE) return Result::IoError;
        if (!insert.prepare(impl_->db, "INSERT INTO linux_waypoint_slots VALUES(?1,?2)")) return Result::IoError;
        ::waypoint::Slot empty;
        empty.generation = 1;
        uint8_t bytes[::waypoint::kEncodedBytes];
        if (!::waypoint::encode(empty, bytes, sizeof(bytes))) return Result::Corrupt;
        for (uint16_t index = 0; index < slotCount(); ++index)
            if (sqlite3_bind_int(insert.value, 1, index) != SQLITE_OK || sqlite3_bind_blob(insert.value, 2, bytes, sizeof(bytes), SQLITE_TRANSIENT) != SQLITE_OK ||
                sqlite3_step(insert.value) != SQLITE_DONE || sqlite3_reset(insert.value) != SQLITE_OK) return Result::IoError;
        if (!transaction.commit()) return Result::IoError;
    }
    else if (count != 2) return Result::Corrupt;
    auto result = impl_->header();
    if (result != Result::Ok) return result;
    Statement slots;
    if (!slots.prepare(impl_->db, "SELECT slot FROM linux_waypoint_slots ORDER BY slot")) return Result::Corrupt;
    uint16_t expected = 0;
    int step;
    while ((step = sqlite3_step(slots.value)) == SQLITE_ROW)
    {
        if (expected >= slotCount() || sqlite3_column_type(slots.value, 0) != SQLITE_INTEGER || sqlite3_column_int64(slots.value, 0) != expected++) return Result::Corrupt;
    }
    if (step != SQLITE_DONE) return Result::IoError;
    if (expected != slotCount()) return Result::Corrupt;
    impl_->ready = true;
    return Result::Ok;
}
Result LinuxWaypointStore::read(uint16_t index, ::waypoint::Record& out)
{
    out = {};
    if (index >= slotCount()) return Result::Invalid;
    if (!impl_->ready) return Result::IoError;
    auto result = impl_->header();
    ::waypoint::Slot slot;
    if (result == Result::Ok) result = impl_->read(index, slot);
    if (result == Result::Ok && slot.state == ::waypoint::SlotState::Active) out = slot.record;
    return result;
}
Result LinuxWaypointStore::create(const ::waypoint::Record& value, uint32_t& id) { return impl_->mutate(value, true, false, id); }
Result LinuxWaypointStore::update(const ::waypoint::Record& value)
{
    uint32_t id;
    return impl_->mutate(value, false, false, id);
}
Result LinuxWaypointStore::remove(uint32_t id)
{
    ::waypoint::Record value;
    value.id = id;
    uint32_t ignored;
    return impl_->mutate(value, false, true, ignored);
}
#else
struct LinuxWaypointStore::Impl
{
};
LinuxWaypointStore::LinuxWaypointStore(std::filesystem::path) : impl_(std::make_unique<Impl>()) {}
LinuxWaypointStore::~LinuxWaypointStore() = default;
Result LinuxWaypointStore::begin() { return Result::IoError; }
Result LinuxWaypointStore::read(uint16_t, ::waypoint::Record& out)
{
    out = {};
    return Result::IoError;
}
Result LinuxWaypointStore::create(const ::waypoint::Record&, uint32_t& id)
{
    id = 0;
    return Result::IoError;
}
Result LinuxWaypointStore::update(const ::waypoint::Record&) { return Result::IoError; }
Result LinuxWaypointStore::remove(uint32_t) { return Result::IoError; }
#endif
} // namespace platform::linux_runtime
