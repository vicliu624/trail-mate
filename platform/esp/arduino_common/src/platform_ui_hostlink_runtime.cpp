#include "platform/ui/hostlink_runtime.h"

#include "platform/esp/arduino_common/hostlink/hostlink_service.h"

namespace platform::ui::hostlink
{

bool is_supported()
{
    return true;
}

void start()
{
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
