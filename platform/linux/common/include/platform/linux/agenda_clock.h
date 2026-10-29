#pragma once
#include "agenda/ports/agenda_clock.h"
#include <functional>
namespace platform::linux_runtime
{
// Worker-owned product clock: local calendar seconds, monotonic time and the
// existing timezone profile. The composition may supply deterministic sources.
class LinuxAgendaClock final : public ::agenda::IAgendaClock
{
  public:
    struct Sources
    {
        std::function<int64_t()> utc;
        std::function<uint64_t()> monotonic;
    };
    explicit LinuxAgendaClock(Sources = {});
    ::agenda::ClockSample sample() const override;

  private:
    Sources sources_;
};
} // namespace platform::linux_runtime
