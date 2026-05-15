#include "linux_sim_runtime_renderer.h"

namespace trailmate::apps::linux_sim_shell
{

bool LinuxSimRuntimeRenderer::render(const LinuxSimRuntimeEntry& entry)
{
    ready_ = false;
    primary_ = false;
    fallback_ = true;

    if (entry.usingPrimaryScreenGraph())
    {
        ready_ = descriptor_renderer_.render(entry.adoption());
        primary_ = ready_;
        fallback_ = !ready_;
        return ready_;
    }

    if (entry.fallbackUsed())
    {
        return renderFallback(entry);
    }

    return false;
}

bool LinuxSimRuntimeRenderer::renderFallback(const LinuxSimRuntimeEntry&)
{
    // Phase 11 fallback: old hardcoded ASCII rendering remains available only
    // when the runtime entry did not provide primary descriptors.
    ready_ = false;
    primary_ = false;
    fallback_ = true;
    return false;
}

bool LinuxSimRuntimeRenderer::ready() const noexcept
{
    return ready_;
}

bool LinuxSimRuntimeRenderer::usingPrimaryScreenGraph() const noexcept
{
    return primary_;
}

bool LinuxSimRuntimeRenderer::usedPrimaryScreenGraph() const noexcept
{
    return primary_;
}

bool LinuxSimRuntimeRenderer::fallbackUsed() const noexcept
{
    return fallback_;
}

bool LinuxSimRuntimeRenderer::usedFallback() const noexcept
{
    return fallback_;
}

std::size_t LinuxSimRuntimeRenderer::lineCount() const noexcept
{
    return ready_ ? descriptor_renderer_.lineCount() : 0;
}

const trailmate::linux_sim::AsciiRenderLine*
LinuxSimRuntimeRenderer::lines() const noexcept
{
    return ready_ ? descriptor_renderer_.lines() : nullptr;
}

const char* LinuxSimRuntimeRenderer::line(std::size_t index) const noexcept
{
    return ready_ ? descriptor_renderer_.line(index) : nullptr;
}

} // namespace trailmate::apps::linux_sim_shell
