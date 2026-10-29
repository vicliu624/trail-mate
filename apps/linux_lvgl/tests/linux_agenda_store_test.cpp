#include "agenda/domain/event_codec.h"
#include "agenda/usecase/agenda_query.h"
#include "agenda/usecase/agenda_service.h"
#include "agenda/usecase/reminder_scheduler.h"
#include "platform/linux/agenda_store.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <unistd.h>

using namespace agenda;
using Store = platform::linux_runtime::LinuxAgendaStore;
struct Database
{
    sqlite3* value = nullptr;
    explicit Database(const std::filesystem::path& path) { assert(sqlite3_open(path.c_str(), &value) == SQLITE_OK); }
    ~Database() { sqlite3_close(value); }
    void sql(const char* text) { assert(sqlite3_exec(value, text, nullptr, nullptr, nullptr) == SQLITE_OK); }
    int64_t scalar(const char* text)
    {
        sqlite3_stmt* statement = nullptr;
        assert(sqlite3_prepare_v2(value, text, -1, &statement, nullptr) == SQLITE_OK);
        assert(sqlite3_step(statement) == SQLITE_ROW);
        const auto result = sqlite3_column_int64(statement, 0);
        assert(sqlite3_step(statement) == SQLITE_DONE);
        sqlite3_finalize(statement);
        return result;
    }
};
EventRecord draft()
{
    EventRecord event;
    event.state = RecordState::Active;
    assert(toCalendarSeconds({2026, 10, 21, 12, 0, 0}, event.start_time));
    event.flags = HasReminder | HasLocation | HasNote;
    event.reminder_offset_sec = 600;
    event.location_type = LocationType::Coordinate;
    event.latitude_e7 = 311234567;
    event.longitude_e7 = 1212345678;
    std::strcpy(event.title, "Meeting");
    std::strcpy(event.note, "中文 note");
    return event;
}
struct Clock final : IAgendaClock
{
    ClockSample now;
    ClockSample sample() const override { return now; }
    void advance(uint64_t seconds)
    {
        now.calendar_seconds += seconds;
        now.monotonic_seconds += seconds;
    }
};
struct Sink final : IReminderSink
{
    Reminder shown;
    unsigned count = 0;
    bool present(const Reminder& value) override
    {
        shown = value;
        ++count;
        return true;
    }
    void withdraw() override { shown = {}; }
};
void blob(Database& db, const uint8_t (&bytes)[kSlotBytes])
{
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(db.value, "UPDATE linux_agenda_slots SET record=?1 WHERE slot=0", -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_bind_blob(statement, 1, bytes, sizeof(bytes), SQLITE_STATIC) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
}
int main()
{
    char root[] = "/tmp/trailmate-agenda-store-XXXXXX";
    assert(mkdtemp(root));
    const auto path = std::filesystem::path(root) / "state.sqlite3";
    {
        Database db(path);
        db.sql("CREATE TABLE unrelated(value INTEGER); INSERT INTO unrelated VALUES(7)");
    }
    const auto event = draft();
    uint32_t first = 0;
    uint32_t highest_deleted = 0;
    {
        Store store(path);
        assert(store.begin() == StoreResult::Ok);
        Store other(path);
        assert(other.begin() == StoreResult::IoError);
        AgendaService service(store);
        assert(service.create(event, first) == AgendaResult::Ok && first == 1);
        EventRecord loaded;
        assert(service.read(first, loaded) == AgendaResult::Ok && loaded.id == first && !std::strcmp(loaded.note, event.note));
        loaded.end_time = loaded.start_time + 3600;
        loaded.flags |= HasEndTime;
        assert(service.update(loaded) == AgendaResult::Ok);
        Database db(path);
        db.sql("CREATE TRIGGER reject_agenda BEFORE UPDATE ON linux_agenda_slots BEGIN SELECT RAISE(ABORT,'injected failure'); END");
        auto changed = loaded;
        std::strcpy(changed.title, "Must not commit");
        assert(service.update(changed) == AgendaResult::StorageError);
        assert(service.read(first, changed) == AgendaResult::Ok && !std::strcmp(changed.title, event.title));
        db.sql("DROP TRIGGER reject_agenda; BEGIN IMMEDIATE");
        const auto start = std::chrono::steady_clock::now();
        assert(service.update(loaded) == AgendaResult::StorageError);
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
        db.sql("ROLLBACK");
        assert(service.update(loaded) == AgendaResult::Ok);
        EventRecord invalid = loaded;
        invalid.title[0] = 0;
        assert(store.writeSlot(0, invalid) == StoreResult::InvalidRecord);
        assert(store.readSlot(store.slotCount(), invalid) == StoreResult::OutOfRange && invalid.id == 0);
        AgendaPage page;
        assert(queryAgenda(store, event.start_time, event.start_time + 86400, 3, {}, page) == StoreResult::Ok && page.count == 1);
        Clock clock;
        clock.now = {event.start_time - 610, 100, 0, true};
        Sink sink;
        ReminderScheduler scheduler(store, clock, sink);
        scheduler.tick();
        assert(!scheduler.showing());
        clock.advance(10);
        scheduler.tick();
        assert(scheduler.showing() && sink.shown.event_id == first);
        assert(scheduler.snooze());
        clock.advance(599);
        scheduler.tick();
        assert(!scheduler.showing());
        clock.advance(1);
        scheduler.tick();
        assert(scheduler.showing() && sink.count == 2);
        assert(scheduler.dismiss());
        scheduler.tick();
        assert(!scheduler.showing());
    }
    {
        Store store(path);
        assert(store.begin() == StoreResult::Ok);
        AgendaService service(store);
        EventRecord loaded;
        assert(service.read(first, loaded) == AgendaResult::Ok && (loaded.flags & HasEndTime));
        assert(store.eraseSlot(0) == StoreResult::Ok);
        assert(store.readSlot(0, loaded) == StoreResult::Ok && loaded.state == RecordState::Deleted && loaded.id == first);
        uint32_t id = 0;
        assert(service.create(event, id) == AgendaResult::Ok && id > first);
        for (unsigned i = 1; i < kMaxActiveEvents; ++i) assert(service.create(event, id) == AgendaResult::Ok);
        const auto highest = id;
        assert(service.create(event, id) == AgendaResult::Full && id == 0);
        assert(service.remove(highest) == AgendaResult::Ok);
        assert(service.create(event, id) == AgendaResult::Ok && id > highest);
        highest_deleted = id;
        for (uint16_t slot = 0; slot < store.slotCount(); ++slot)
        {
            assert(store.readSlot(slot, loaded) == StoreResult::Ok);
            if (loaded.state == RecordState::Active) assert(service.remove(loaded.id) == AgendaResult::Ok);
        }
    }
    {
        Store store(path);
        assert(store.begin() == StoreResult::Ok);
        AgendaService service(store);
        uint32_t created = 0;
        assert(service.create(event, created) == AgendaResult::Ok && created > highest_deleted);
    }
    const auto fault = [&](const char* name, const char* sql, StoreResult expected)
    {
        const auto copy = std::filesystem::path(root) / name;
        std::filesystem::copy_file(path, copy);
        {
            Database db(copy);
            db.sql(sql);
        }
        Store store(copy);
        assert(store.begin() == expected);
        EventRecord out;
        out.id = 123;
        assert(store.readSlot(0, out) == StoreResult::IoError && out.id == 0);
    };
    fault("future.sqlite3", "UPDATE linux_agenda_meta SET version=99", StoreResult::UnsupportedVersion);
    {
        Database db(std::filesystem::path(root) / "future.sqlite3");
        assert(db.scalar("SELECT version FROM linux_agenda_meta") == 99);
    }
    fault("missing.sqlite3", "DELETE FROM linux_agenda_slots WHERE slot=0", StoreResult::Corrupt);
    fault("table.sqlite3", "DROP TABLE linux_agenda_slots", StoreResult::Corrupt);
    const auto short_record = std::filesystem::path(root) / "short.sqlite3";
    std::filesystem::copy_file(path, short_record);
    {
        Database db(short_record);
        db.sql("UPDATE linux_agenda_slots SET record=zeroblob(7) WHERE slot=0");
    }
    {
        Store store(short_record);
        EventRecord out;
        assert(store.begin() == StoreResult::Ok && store.readSlot(0, out) == StoreResult::Corrupt && out.id == 0);
    }
    const auto future_record = std::filesystem::path(root) / "future-record.sqlite3";
    std::filesystem::copy_file(path, future_record);
    uint8_t encoded[kSlotBytes];
    auto future = event;
    future.id = 42;
    assert(encodeEvent(future, encoded));
    encoded[2] = 2;
    const auto crc = recordCrc32(encoded, kSlotBytes - 4);
    for (unsigned i = 0; i < 4; ++i) encoded[kSlotBytes - 4 + i] = uint8_t(crc >> (i * 8));
    {
        Database db(future_record);
        blob(db, encoded);
    }
    {
        Store store(future_record);
        EventRecord out;
        assert(store.begin() == StoreResult::Ok);
        assert(store.readSlot(0, out) == StoreResult::UnsupportedVersion && out.id == 0);
        assert(store.writeSlot(0, future) == StoreResult::UnsupportedVersion);
    }
    const auto corrupt_record = std::filesystem::path(root) / "crc.sqlite3";
    std::filesystem::copy_file(path, corrupt_record);
    encoded[40] ^= 1;
    {
        Database db(corrupt_record);
        blob(db, encoded);
    }
    {
        Store store(corrupt_record);
        EventRecord out;
        assert(store.begin() == StoreResult::Ok && store.readSlot(0, out) == StoreResult::Corrupt && out.id == 0);
        assert(store.writeSlot(0, future) == StoreResult::Corrupt);
        auto healthy = event;
        healthy.id = 100;
        assert(store.writeSlot(1, healthy) == StoreResult::Ok);
        Clock clock;
        clock.now = {event.start_time - 610, 100, 0, true};
        Sink sink;
        ReminderScheduler scheduler(store, clock, sink);
        scheduler.tick();
        clock.advance(10);
        scheduler.tick();
        assert(scheduler.status() == StoreResult::Corrupt && scheduler.showing() && sink.shown.event_id == 100);
    }
    {
        Database db(path);
        assert(db.scalar("SELECT value FROM unrelated") == 7);
    }
    std::filesystem::remove_all(root);
}
