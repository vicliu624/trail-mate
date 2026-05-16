#pragma once

namespace trailmate::apps::linux_uconsole_gtk
{

struct LinuxUConsoleGtkHistoricalSourceDescriptor
{
    const char* historical_root_name = "removed root linux_uconsole";
    const char* historical_role = "pre-refactor uConsole GTK implementation root";
    const char* replacement_owner = "apps/linux_uconsole_gtk + modules/ui_gtk_runtime";
};

const LinuxUConsoleGtkHistoricalSourceDescriptor&
linuxUConsoleGtkHistoricalSourceDescriptor();

} // namespace trailmate::apps::linux_uconsole_gtk
