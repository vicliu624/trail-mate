#include "linux_uconsole_gtk_page_registry_renderer.h"

namespace trailmate::apps::linux_uconsole_gtk
{

bool LinuxUConsoleGtkPageRegistryRenderer::render(
    const LinuxUConsoleGtkPageRegistryAdoption& adoption)
{
    ready_ = false;
    primary_ = false;
    fallback_ = true;

    if (adoption.usingPrimaryScreenGraph())
    {
        ready_ = registry_.load(adoption.adoption());
        primary_ = ready_;
        fallback_ = !ready_;
        return ready_;
    }

    if (adoption.fallbackUsed())
    {
        return renderFallback(adoption);
    }

    return false;
}

bool LinuxUConsoleGtkPageRegistryRenderer::renderFallback(
    const LinuxUConsoleGtkPageRegistryAdoption&)
{
    // Phase 11 fallback: hardcoded GTK page registry remains available only
    // when descriptor registry consumption is unavailable.
    ready_ = false;
    primary_ = false;
    fallback_ = true;
    return false;
}

bool LinuxUConsoleGtkPageRegistryRenderer::ready() const noexcept
{
    return ready_;
}

bool LinuxUConsoleGtkPageRegistryRenderer::usingPrimaryScreenGraph()
    const noexcept
{
    return primary_;
}

bool LinuxUConsoleGtkPageRegistryRenderer::usedPrimaryScreenGraph()
    const noexcept
{
    return primary_;
}

bool LinuxUConsoleGtkPageRegistryRenderer::fallbackUsed() const noexcept
{
    return fallback_;
}

bool LinuxUConsoleGtkPageRegistryRenderer::usedFallback() const noexcept
{
    return fallback_;
}

std::size_t LinuxUConsoleGtkPageRegistryRenderer::pageCount() const noexcept
{
    return ready_ ? registry_.pageCount() : 0;
}

const trailmate::uconsole::GtkDescriptorPage*
LinuxUConsoleGtkPageRegistryRenderer::pages() const noexcept
{
    return ready_ ? registry_.pages() : nullptr;
}

} // namespace trailmate::apps::linux_uconsole_gtk
