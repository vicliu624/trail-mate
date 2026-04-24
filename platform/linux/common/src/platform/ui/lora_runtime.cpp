#include "platform/ui/lora_runtime.h"

#include <limits>

namespace platform::ui::lora
{

bool is_supported()
{
    return false;
}

bool acquire()
{
    return false;
}

bool is_online()
{
    return false;
}

bool configure_receive(float, const ReceiveConfig&)
{
    return false;
}

float read_instant_rssi()
{
    return std::numeric_limits<float>::quiet_NaN();
}

void release()
{
}

} // namespace platform::ui::lora
