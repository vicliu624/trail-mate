#include "linux_uconsole_gtk_historical_source_descriptor.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& descriptor =
        trailmate::apps::linux_uconsole_gtk::
            linuxUConsoleGtkHistoricalSourceDescriptor();

    assert(std::strcmp(descriptor.historical_root_name,
                       "removed root linux_uconsole") == 0);
    assert(std::strcmp(descriptor.historical_role,
                       "pre-refactor uConsole GTK implementation root") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/linux_uconsole_gtk + modules/ui_gtk_runtime") == 0);
    return 0;
}
