#include "linux_sim_runtime_entry.h"

namespace trailmate::apps::linux_sim_shell
{

bool LinuxSimRuntimeEntry::start(const LinuxSimAppShell& shell)
{
    ready_ = adoption_probe_.load(shell);
    // Phase 9 fallback: hardcoded simulator routing remains until this runtime
    // entry is the only simulator route source.
    fallback_ = !ready_;
    return ready_;
}

bool LinuxSimRuntimeEntry::ready() const
{
    return ready_;
}

bool LinuxSimRuntimeEntry::fallbackUsed() const
{
    return fallback_;
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
