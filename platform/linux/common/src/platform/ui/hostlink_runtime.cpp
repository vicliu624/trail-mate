#include "platform/ui/hostlink_runtime.h"

namespace platform::ui::hostlink
{

bool is_supported()
{
    return false;
}

void start()
{
}

void stop()
{
}

bool is_active()
{
    return false;
}

Status get_status()
{
    return {};
}

} // namespace platform::ui::hostlink
