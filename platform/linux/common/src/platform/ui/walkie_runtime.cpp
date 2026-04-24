#include "platform/ui/walkie_runtime.h"

namespace platform::ui::walkie
{
namespace
{

constexpr const char* kUnsupportedMessage = "Walkie-talkie unsupported on Linux";

} // namespace

bool is_supported()
{
    return false;
}

bool start()
{
    return false;
}

void stop()
{
}

bool is_active()
{
    return false;
}

int volume()
{
    return 0;
}

Status get_status()
{
    return {};
}

const char* last_error()
{
    return kUnsupportedMessage;
}

} // namespace platform::ui::walkie
