#pragma once
#include "ui_presentation/waypoint/waypoint_model.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// Root-owned asynchronous persistence. Reads use a sealed memory-file
// projection; a pending write retains only one record and its result.
class LinuxWaypointSource final : public ::ui::waypoint::ISource, public ::ui::waypoint::IActionSink
{
  public:
    explicit LinuxWaypointSource(std::filesystem::path database);
    ~LinuxWaypointSource() override;
    void shutdown(); // Drain an accepted command before joining.
    bool ready() const;
    ::waypoint::Result page(uint32_t, uint8_t, ::waypoint::Page&) override;
    ::waypoint::Result detail(uint32_t, ::waypoint::Record&) override;
    ::ui::waypoint::CommandResult commandResult() const override;
    ::ui::UiActionResult save(const ::waypoint::Record&) override;
    ::ui::UiActionResult remove(uint32_t) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
