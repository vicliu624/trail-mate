#include "platform/linux/waypoint_source.h"
#include "platform/linux/waypoint_store.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
using namespace platform::linux_runtime;
using waypoint::Result;
using State = ui::waypoint::CommandState;
template <class Predicate>
void until(Predicate predicate)
{
    const auto end = std::chrono::steady_clock::now() + 8s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(5ms);
    }
}
struct Database
{
    sqlite3* db = nullptr;
    explicit Database(const std::filesystem::path& path) { assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK); }
    ~Database() { sqlite3_close(db); }
    void exec(const char* sql) { assert(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
};
waypoint::Record record(const char* name = "Camp 中文")
{
    waypoint::Record value;
    std::strcpy(value.name, name);
    value.latitude_e7 = 225432100;
    value.longitude_e7 = 1141234567;
    return value;
}
int main()
{
    char root[] = "/tmp/trailmate-waypoint-XXXXXX";
    assert(mkdtemp(root));
    const auto path = std::filesystem::path(root) / "state.db";
    uint32_t first = 0, highest = 0;
    {
        LinuxWaypointStore store(path);
        assert(store.begin() == Result::Ok);
        LinuxWaypointStore competing(path);
        assert(competing.begin() == Result::IoError);
        waypoint::Record out;
        assert(store.read(0, out) == Result::Ok && !out.id);
        assert(store.read(waypoint::kMaxWaypoints, out) == Result::Invalid);
        auto value = record();
        auto invalid = value;
        invalid.latitude_e7 = 900000001;
        uint32_t id = 99;
        assert(store.create(invalid, id) == Result::Invalid && !id);
        assert(store.create(value, first) == Result::Ok && first);
        value.id = first;
        value.longitude_e7 = -1800000000;
        assert(store.update(value) == Result::Ok);
        assert(waypoint::find(store, first, out) == Result::Ok && out.longitude_e7 == value.longitude_e7 && !std::strcmp(out.name, value.name));
        for (unsigned index = 1; index < waypoint::kMaxWaypoints; ++index)
            assert(store.create(record(), highest) == Result::Ok);
        assert(store.create(record(), id) == Result::Full && !id);
        waypoint::Page page;
        assert(waypoint::query(store, 0, 5, page) == Result::Ok && page.count == 5 && page.has_more && page.rows[0].id == first);
        assert(store.remove(first) == Result::Ok);
        assert(store.create(record("Replacement"), id) == Result::Ok && id > highest);
        highest = id;
        for (uint32_t index = first + 1; index <= highest; ++index) assert(store.remove(index) == Result::Ok);
        assert(store.remove(first) == Result::NotFound);
    }
    {
        LinuxWaypointStore store(path);
        assert(store.begin() == Result::Ok);
        uint32_t id = 0;
        assert(store.create(record(), id) == Result::Ok && id > highest);
        highest = id;
        Database db(path);
        db.exec("CREATE TRIGGER fail_waypoint BEFORE UPDATE ON linux_waypoint_slots BEGIN SELECT RAISE(ABORT,'test'); END;");
        uint32_t failed = 17;
        assert(store.create(record("Failed"), failed) == Result::IoError && !failed);
        db.exec("DROP TRIGGER fail_waypoint;");
        assert(store.create(record("Next"), id) == Result::Ok && id == highest + 1);
        highest = id;
    }
    {
        LinuxWaypointSource source(path);
        until([&]
              { return source.ready(); });
        auto value = record("Actor");
        assert(source.save(value).ok);
        until([&]
              { return source.commandResult().state != State::Pending; });
        auto result = source.commandResult();
        assert(result.state == State::Succeeded && result.id > highest);
        const auto actor_id = result.id;
        waypoint::Record out;
        assert(source.detail(actor_id, out) == Result::Ok && !std::strcmp(out.name, "Actor"));
        Database db(path);
        db.exec("BEGIN IMMEDIATE;");
        assert(source.remove(actor_id).ok);
        assert(!source.save(value).ok);
        const auto start = std::chrono::steady_clock::now();
        assert(source.detail(actor_id, out) == Result::Ok);
        assert(std::chrono::steady_clock::now() - start < 100ms);
        until([&]
              { return source.commandResult().state != State::Pending; });
        assert(source.commandResult().state == State::Failed && source.commandResult().result == Result::IoError);
        db.exec("ROLLBACK;");
        assert(source.remove(actor_id).ok);
        until([&]
              { return source.commandResult().state != State::Pending; });
        assert(source.commandResult().state == State::Succeeded);
        assert(source.detail(actor_id, out) == Result::NotFound);
        assert(source.save(record("Shutdown drain")).ok);
        source.shutdown();
        result = source.commandResult();
        assert(result.state == State::Succeeded && result.id > actor_id);
        highest = result.id;
        assert(!source.ready() && source.detail(highest, out) == Result::IoError && !source.save(record()).ok);
    }
    {
        LinuxWaypointStore store(path);
        assert(store.begin() == Result::Ok);
        waypoint::Record out;
        assert(waypoint::find(store, highest, out) == Result::Ok && !std::strcmp(out.name, "Shutdown drain"));
        Database db(path);
        db.exec("UPDATE linux_waypoint_slots SET record=zeroblob(64) WHERE slot=0;");
        waypoint::Page page;
        assert(waypoint::query(store, 0, 5, page) == Result::Corrupt && !page.count);
        uint32_t id = 99;
        assert(store.create(record(), id) == Result::Corrupt && !id);
        db.exec("UPDATE linux_waypoint_meta SET version=2;");
        assert(store.begin() == Result::Corrupt);
    }
    {
        LinuxWaypointSource source(path);
        std::this_thread::sleep_for(200ms);
        assert(!source.ready() && !source.save(record()).ok);
        Database db(path);
        db.exec("UPDATE linux_waypoint_meta SET version=1; DELETE FROM linux_waypoint_slots WHERE slot=63;");
    }
    {
        LinuxWaypointStore store(path);
        assert(store.begin() == Result::Corrupt);
    }
    std::filesystem::remove_all(root);
}
