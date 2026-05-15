#include "linux_uconsole_gtk_legacy_source_descriptor.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& descriptor =
        trailmate::apps::linux_uconsole_gtk::
            linuxUConsoleGtkLegacySourceDescriptor();

    assert(std::strcmp(descriptor.root_path,
                       "legacy/app_implementations/linux_uconsole") == 0);
    assert(std::strcmp(descriptor.historical_name, "linux_uconsole") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/linux_uconsole_gtk") == 0);
    assert(descriptor.compatibility_required);
    return 0;
}
