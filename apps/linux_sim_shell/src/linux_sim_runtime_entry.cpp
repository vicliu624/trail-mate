#include "linux_sim_runtime_entry.h"

namespace trailmate::apps::linux_sim_shell
{

bool LinuxSimRuntimeEntry::start(const LinuxSimAppShell& shell)
{
    ready_ = adoption_probe_.load(shell);
    if (ready_)
    {
        runtime_source_ = LinuxSimRuntimeSource::ScreenGraphAdoption;
        fallback_ = false;
        return true;
    }

    runtime_source_ = LinuxSimRuntimeSource::HardcodedFallback;
    fallback_ = true;
    return startFallback(shell);
}

bool LinuxSimRuntimeEntry::startFallback(const LinuxSimAppShell&)
{
    // Phase 10 fallback containment: old simulator routing remains available
    // until the renderer fully consumes AsciiRuntimeEntryAdoption descriptors.
    return false;
}

bool LinuxSimRuntimeEntry::ready() const
{
    return ready_;
}

bool LinuxSimRuntimeEntry::usingPrimaryScreenGraph() const noexcept
{
    return runtime_source_ == LinuxSimRuntimeSource::ScreenGraphAdoption &&
           ready_ && !fallback_;
}

bool LinuxSimRuntimeEntry::fallbackUsed() const
{
    return fallback_;
}

LinuxSimRuntimeSource LinuxSimRuntimeEntry::runtimeSource() const noexcept
{
    return runtime_source_;
}

std::size_t LinuxSimRuntimeEntry::menuCount() const
{
    return ready_ ? adoption_probe_.menuCount() : 0;
}

std::size_t LinuxSimRuntimeEntry::screenCount() const
{
    return ready_ ? adoption_probe_.screenCount() : 0;
}

const trailmate::linux_sim::AsciiRuntimeEntryAdoption&
LinuxSimRuntimeEntry::adoption() const
{
    return adoption_probe_.adoption();
}

} // namespace trailmate::apps::linux_sim_shell
