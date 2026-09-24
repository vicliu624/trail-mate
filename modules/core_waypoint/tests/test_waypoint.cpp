#include "waypoint/waypoint.h"
#include <cassert>
#include <cstring>

class Store final : public waypoint::IStore
{
  public:
    waypoint::Record rows[64]{}; // Test fixture only, never a production cache.
    int failing = -1;
    uint16_t slotCount() const override { return 64; }
    waypoint::Result read(uint16_t slot, waypoint::Record& out) override
    {
        if (slot == failing) return waypoint::Result::IoError;
        out = rows[slot];
        return waypoint::Result::Ok;
    }
    waypoint::Result create(const waypoint::Record&, uint32_t&) override { return waypoint::Result::IoError; }
    waypoint::Result update(const waypoint::Record&) override { return waypoint::Result::IoError; }
    waypoint::Result remove(uint32_t) override { return waypoint::Result::IoError; }
};

int main()
{
    Store store;
    waypoint::Page page;
    using waypoint::Result;
    assert(waypoint::query(store, 0, 5, page) == Result::Ok && !page.count && !page.has_more);
    for (unsigned i = 0; i < 64; ++i)
    {
        store.rows[i].id = 64 - i;
        std::strcpy(store.rows[i].name, "Campsite");
    }
    uint32_t after = 0;
    unsigned total = 0;
    do
    {
        assert(waypoint::query(store, after, 5, page) == Result::Ok);
        for (unsigned i = 0; i < page.count; ++i) assert(page.rows[i].id == ++after);
        total += page.count;
    } while (page.has_more);
    assert(total == 64);
    assert(waypoint::query(store, 0, 0, page) == Result::Invalid && !page.count);
    assert(waypoint::query(store, 0, 6, page) == Result::Invalid);
    waypoint::Record record;
    assert(waypoint::find(store, 10, record) == Result::Ok && record.id == 10);
    assert(waypoint::find(store, 100, record) == Result::NotFound && !record.id);
    store.failing = 12;
    assert(waypoint::query(store, 0, 5, page) == Result::IoError && !page.count);
    store.failing = -1;
    store.rows[12].latitude_e7 = 900000001;
    assert(waypoint::query(store, 0, 5, page) == Result::Corrupt && !page.count);
    store.rows[12].latitude_e7 = -900000000;
    store.rows[12].longitude_e7 = 1800000000;
    assert(waypoint::valid(store.rows[12]));
    std::memset(store.rows[12].name, 'x', sizeof(store.rows[12].name));
    assert(!waypoint::valid(store.rows[12]));
    const char* invalid_names[] = {"", "\x80", "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80",
                                   "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
                                   "\xc2", "\xe4\xb8", "\xf0\x9f\x8f", "\xc2x"};
    for (const auto* name : invalid_names)
    {
        std::strcpy(store.rows[12].name, name);
        assert(!waypoint::valid(store.rows[12]));
        assert(waypoint::query(store, 0, 5, page) == Result::Corrupt && !page.count);
    }
    const char* valid_names[] = {"Camp", "\xc2\x80", "\xe0\xa0\x80", "\xed\x9f\xbf",
                                 "\xee\x80\x80", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf",
                                 u8"山间营地"};
    for (const auto* name : valid_names)
    {
        std::strcpy(store.rows[12].name, name);
        assert(waypoint::valid(store.rows[12]));
    }
    std::memset(store.rows[12].name, 'x', 27);
    std::memcpy(store.rows[12].name + 27, "\xf0\x9f\x8f\x95", 5);
    assert(waypoint::valid(store.rows[12])); // Four-byte scalar ending at byte 31.
    std::memset(store.rows[12].name, 'x', 28);
    std::memcpy(store.rows[12].name + 28, "\xf0\x9f\x8f", 4);
    assert(!waypoint::valid(store.rows[12])); // Truncated at the field boundary.
}
