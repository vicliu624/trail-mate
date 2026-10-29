#pragma once
#include "waypoint/waypoint.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// Worker-owned connection. Success is reported only after a durable commit.
class LinuxWaypointStore final : public ::waypoint::IStore
{
  public:
    explicit LinuxWaypointStore(std::filesystem::path database);
    ~LinuxWaypointStore() override;
    ::waypoint::Result begin();
    uint16_t slotCount() const override { return ::waypoint::kMaxWaypoints; }
    ::waypoint::Result read(uint16_t, ::waypoint::Record&) override;
    ::waypoint::Result create(const ::waypoint::Record&, uint32_t&) override;
    ::waypoint::Result update(const ::waypoint::Record&) override;
    ::waypoint::Result remove(uint32_t) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
