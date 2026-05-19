#include "platform/ui/settings_backup_runtime.h"

#include <cstdio>

namespace platform::ui::settings_backup
{

bool is_supported()
{
    return false;
}

Status status()
{
    Status out{};
    out.supported = false;
    std::snprintf(out.message, sizeof(out.message), "%s", "Settings backup unsupported");
    return out;
}

bool backup()
{
    return false;
}

bool restore()
{
    return false;
}

bool remove()
{
    return false;
}

} // namespace platform::ui::settings_backup
