#include "platform/esp/common/storage/waypoint_file_store.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    using platform::esp::storage::WaypointFileStore;
    using waypoint::Result;
    const char* path = "waypoint-test.dat";
    const char* staging = "waypoint-test.init";
    std::remove(path);
    std::remove(staging);
    WaypointFileStore store(path, staging);
    assert(store.begin() == Result::Ok);
    waypoint::Record record;
    std::strcpy(record.name, "Camp");
    record.latitude_e7 = -100;
    uint32_t id = 0;
    assert(store.create(record, id) == Result::Ok && id == 1);
    record.id = id;
    std::strcpy(record.name, "Updated");
    assert(store.update(record) == Result::Ok);
    WaypointFileStore reopened(path, staging);
    assert(reopened.begin() == Result::Ok);
    waypoint::Record loaded;
    assert(waypoint::find(reopened, id, loaded) == Result::Ok && !std::strcmp(loaded.name, "Updated"));
    // Initial bank 0 gen1; create bank1 gen2; update bank0 gen3.
    // Damage only the newest copy: the last intact record must survive.
    auto* file = std::fopen(path, "r+b");
    assert(file && !std::fseek(file, 64 + 24, SEEK_SET));
    assert(std::fputc('X', file) != EOF && !std::fclose(file));
    assert(waypoint::find(reopened, id, loaded) == Result::Ok && !std::strcmp(loaded.name, "Camp"));
    assert(reopened.remove(id) == Result::Ok);
    assert(waypoint::find(reopened, id, loaded) == Result::NotFound);
    record.id = 0;
    assert(reopened.create(record, id) == Result::Ok && id == 2);
    for (unsigned i = 1; i < waypoint::kMaxWaypoints; ++i) assert(reopened.create(record, id) == Result::Ok);
    assert(reopened.create(record, id) == Result::Full && id == 0);
    file = std::fopen(path, "r+b");
    assert(file && std::fputc('X', file) != EOF && !std::fclose(file));
    assert(reopened.begin() == Result::Corrupt); // Never reset/format a damaged file.
    assert(!std::remove(path));
    std::remove(staging);
}
