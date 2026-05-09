#include "platform/ui/firmware_update_runtime.h"

#include <cstdio>

namespace platform::ui::firmware_update
{

bool is_supported()
{
    return false;
}

Status status()
{
    Status out{};
    out.supported = false;
    out.busy = false;
    out.checked = false;
    out.update_available = false;
    out.direct_ota = false;
    out.progress_percent = -1;
    out.phase = Phase::Unsupported;
    std::snprintf(out.message, sizeof(out.message), "%s", "Firmware update unsupported on Linux");
    return out;
}

bool start_check()
{
    return false;
}

bool start_install()
{
    return false;
}

} // namespace platform::ui::firmware_update
