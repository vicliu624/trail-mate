#include "uconsole_legacy_implementation_adapter.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_uconsole::UConsoleLegacyImplementationAdapter adapter;
    assert(adapter.validate());

    const auto& descriptor = adapter.descriptor();
    assert(std::strcmp(descriptor.implementation_root,
                       "legacy/app_implementations/linux_uconsole") == 0);
    assert(std::strcmp(descriptor.app_shell, "apps/linux_uconsole_gtk") == 0);
    assert(std::strcmp(descriptor.target_id, "uconsole") == 0);
    return 0;
}
