#include "platform/ui/network_runtime.h"
#include "platform/ui/wifi_runtime.h"

namespace platform::ui::network
{
Status status()
{
    const auto wireless = wifi::status();
    return {wireless.supported, wireless.connected};
}
} // namespace platform::ui::network
