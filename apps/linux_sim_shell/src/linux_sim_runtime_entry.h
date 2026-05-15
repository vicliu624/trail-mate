#pragma once

#include "linux_sim_app_shell.h"
#include "linux_sim_runtime_entry_adoption_probe.h"
#include "ui_ascii_runtime/ascii_runtime_entry_adoption.h"

#include <cstddef>

namespace trailmate::apps::linux_sim_shell
{

enum class LinuxSimRuntimeSource
{
    ScreenGraphAdoption,
    HardcodedFallback,
};

class LinuxSimRuntimeEntry
{
  public:
    bool start(const LinuxSimAppShell& shell);

    bool ready() const;
    bool usingPrimaryScreenGraph() const noexcept;
    bool fallbackUsed() const;
    LinuxSimRuntimeSource runtimeSource() const noexcept;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const trailmate::linux_sim::AsciiRuntimeEntryAdoption& adoption() const;

  private:
    bool startFallback(const LinuxSimAppShell& shell);

  private:
    LinuxSimRuntimeEntryAdoptionProbe adoption_probe_{};
    LinuxSimRuntimeSource runtime_source_ =
        LinuxSimRuntimeSource::HardcodedFallback;
    bool ready_ = false;
    bool fallback_ = true;
};

} // namespace trailmate::apps::linux_sim_shell
