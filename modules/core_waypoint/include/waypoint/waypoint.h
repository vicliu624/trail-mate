#pragma once

#include <cstdint>

namespace waypoint
{
constexpr uint16_t kMaxWaypoints = 64;
constexpr uint8_t kPageCapacity = 5;

// Product data, not the on-disk representation. Zero id denotes an empty slot.
struct Record
{
    uint32_t id = 0;
    int32_t latitude_e7 = 0;
    int32_t longitude_e7 = 0;
    char name[32]{};
};
static_assert(sizeof(Record) == 44);

enum class Result : uint8_t
{
    Ok,
    Invalid,
    NotFound,
    Full,
    Corrupt,
    IoError
};

class IStore
{
  public:
    virtual ~IStore() = default;
    virtual uint16_t slotCount() const = 0;
    virtual Result read(uint16_t slot, Record& out) = 0;
    // Allocate a durable, non-reused id on successful creation. Failed writes
    // must not publish an id. Updates preserve the existing id.
    virtual Result create(const Record& value, uint32_t& id) = 0;
    virtual Result update(const Record& value) = 0;
    virtual Result remove(uint32_t id) = 0;
};

struct Page
{
    Record rows[kPageCapacity]{};
    uint8_t count = 0;
    bool has_more = false;
};
static_assert(sizeof(Page) < 256, "Waypoint picker retains only one visible page");

bool valid(const Record& record);
// Stable ascending id order, independent of slot reuse. No retained database.
Result query(IStore& store, uint32_t after_id, uint8_t capacity, Page& out);
Result find(IStore& store, uint32_t id, Record& out);
} // namespace waypoint
