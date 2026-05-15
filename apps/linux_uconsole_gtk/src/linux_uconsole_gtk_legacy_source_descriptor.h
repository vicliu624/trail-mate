#pragma once

namespace trailmate::apps::linux_uconsole_gtk
{

struct LinuxUConsoleGtkLegacySourceDescriptor
{
    const char* root_path = "legacy/app_implementations/linux_uconsole";
    const char* historical_name = "linux_uconsole";
    const char* replacement_owner = "apps/linux_uconsole_gtk";
    bool compatibility_required = true;
};

const LinuxUConsoleGtkLegacySourceDescriptor&
linuxUConsoleGtkLegacySourceDescriptor();

} // namespace trailmate::apps::linux_uconsole_gtk
