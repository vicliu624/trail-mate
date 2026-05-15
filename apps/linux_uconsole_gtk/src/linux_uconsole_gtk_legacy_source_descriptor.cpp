#include "linux_uconsole_gtk_legacy_source_descriptor.h"

namespace trailmate::apps::linux_uconsole_gtk
{

const LinuxUConsoleGtkLegacySourceDescriptor&
linuxUConsoleGtkLegacySourceDescriptor()
{
    static const LinuxUConsoleGtkLegacySourceDescriptor descriptor{};
    return descriptor;
}

} // namespace trailmate::apps::linux_uconsole_gtk
