#pragma once
#include "platform/ui/tmap_creation_runtime.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
struct TmapCreationOptions
{
    std::filesystem::path storage, cache;
    // An installed executable or an explicit composition/test override.
    std::filesystem::path producer = "/usr/lib/trailmate/tmap-producer/TmapTool";
};
class LinuxTmapCreation final : public ::platform::ui::tmap_creation::Service
{
  public:
    explicit LinuxTmapCreation(TmapCreationOptions);
    ~LinuxTmapCreation();
    std::vector<::platform::ui::tmap_creation::Source> sources() override;
    ::platform::ui::tmap_creation::Estimate estimate(const ::platform::ui::tmap_creation::Plan&) override;
    bool start(const ::platform::ui::tmap_creation::Plan&, std::string& error) override;
    ::platform::ui::tmap_creation::Snapshot snapshot() override;
    void cancel() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Called by device composition; the service is retired before application exit.
void configure_tmap_creation(bool enabled);
class TmapCreationBinding
{
  public:
    explicit TmapCreationBinding(bool enabled) { configure_tmap_creation(enabled); }
    ~TmapCreationBinding() { configure_tmap_creation(false); }
    TmapCreationBinding(const TmapCreationBinding&) = delete;
    TmapCreationBinding& operator=(const TmapCreationBinding&) = delete;
};
} // namespace platform::linux_runtime
