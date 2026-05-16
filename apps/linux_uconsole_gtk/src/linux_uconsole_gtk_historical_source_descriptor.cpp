#include "linux_uconsole_gtk_historical_source_descriptor.h"

namespace trailmate::apps::linux_uconsole_gtk
{

const LinuxUConsoleGtkHistoricalSourceDescriptor&
linuxUConsoleGtkHistoricalSourceDescriptor()
{
    static const LinuxUConsoleGtkHistoricalSourceDescriptor descriptor{};
    return descriptor;
}

} // namespace trailmate::apps::linux_uconsole_gtk
