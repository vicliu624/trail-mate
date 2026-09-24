#pragma once
#include "platform/esp/common/storage/stdio_record_file_io.h"
#include "waypoint/record_codec.h"

namespace platform::esp::storage
{
// Single-owner record store over injected file transport. Caller prepares the
// directory. No formatting, bus policy or retained file handle.
class WaypointFileStore final : public waypoint::IStore
{
  public:
    static constexpr long kFileBytes = 64 + 64 * 2 * waypoint::kMaxWaypoints;
    WaypointFileStore(const char* path, const char* staging, RecordFileIo& io = stdioRecordFileIo()) : io_(io), path_(path), staging_(staging) {}
    waypoint::Result begin();
    uint16_t slotCount() const override { return waypoint::kMaxWaypoints; }
    waypoint::Result read(uint16_t slot, waypoint::Record& out) override;
    waypoint::Result create(const waypoint::Record& value, uint32_t& id) override;
    waypoint::Result update(const waypoint::Record& value) override;
    waypoint::Result remove(uint32_t id) override;

  private:
    waypoint::Result choose(RecordFileIo::Handle file, uint16_t slot, waypoint::Slot& out, uint8_t& bank);
    waypoint::Result mutate(const waypoint::Record& value, bool creating, bool deleting, uint32_t& id);
    RecordFileIo& io_;
    const char* path_;
    const char* staging_;
    uint8_t bytes_[waypoint::kEncodedBytes]{};
    bool ready_ = false;
};
static_assert(sizeof(WaypointFileStore) <= (sizeof(void*) == 4 ? 96 : 104),
              "Waypoint store must not retain a database");
} // namespace platform::esp::storage
