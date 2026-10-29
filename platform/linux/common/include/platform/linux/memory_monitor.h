#pragma once

#include "platform/ui/device_runtime.h"
#include <chrono>
#include <functional>
#include <istream>
#include <memory>

namespace platform::linux_runtime
{
// A complete, bounded Linux meminfo sample. Missing/invalid fields yield an
// unavailable (zero) snapshot, never a mixture of separate file reads.
platform::ui::device::MemoryStats parse_linux_meminfo(std::istream& input);
class MemoryMonitor
{
  public:
    using Sampler = std::function<platform::ui::device::MemoryStats()>;
    explicit MemoryMonitor(Sampler sampler = {}, std::chrono::milliseconds interval = std::chrono::seconds(1));
    ~MemoryMonitor();
    platform::ui::device::MemoryStats snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
platform::ui::device::MemoryStats linux_memory_snapshot();
} // namespace platform::linux_runtime
