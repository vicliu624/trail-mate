#include "platform/ui/hostlink_runtime.h"

#include "sdkconfig.h"

#include "platform/esp/arduino_common/hostlink/hostlink_service.h"

namespace platform::ui::hostlink
{

bool is_supported()
{
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    return true;
#else
    return false;
#endif
}

void start()
{
    if (!is_supported())
    {
        return;
    }
    ::hostlink::start();
}

void stop()
{
    ::hostlink::stop();
}

bool is_active()
{
    return ::hostlink::is_active();
}

Status get_status()
{
    return ::hostlink::get_status();
}

} // namespace platform::ui::hostlink
