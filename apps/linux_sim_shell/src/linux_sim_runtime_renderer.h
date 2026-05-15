#pragma once

#include "linux_sim_runtime_entry.h"
#include "ui_ascii_runtime/ascii_descriptor_renderer.h"

#include <cstddef>

namespace trailmate::apps::linux_sim_shell
{

class LinuxSimRuntimeRenderer
{
  public:
    bool render(const LinuxSimRuntimeEntry& entry);

    bool ready() const noexcept;
    bool usingPrimaryScreenGraph() const noexcept;
    bool usedPrimaryScreenGraph() const noexcept;
    bool fallbackUsed() const noexcept;
    bool usedFallback() const noexcept;
    std::size_t lineCount() const noexcept;
    const trailmate::linux_sim::AsciiRenderLine* lines() const noexcept;
    const char* line(std::size_t index) const noexcept;

  private:
    bool renderFallback(const LinuxSimRuntimeEntry& entry);

  private:
    trailmate::linux_sim::AsciiDescriptorRenderer descriptor_renderer_{};
    bool ready_ = false;
    bool primary_ = false;
    bool fallback_ = true;
};

} // namespace trailmate::apps::linux_sim_shell
