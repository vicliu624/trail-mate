#include "platform/ui/walkie_runtime.h"

#include "platform/esp/common/walkie_runtime.h"
#include "walkie/walkie_service.h"

namespace platform::ui::walkie
{

bool is_supported()
{
    return ::platform::esp::common::walkie_runtime::isSupported();
}

bool start()
{
    return ::walkie::start();
}

void stop()
{
    ::walkie::stop();
}

bool is_active()
{
    return ::walkie::is_active();
}

int volume()
{
    return ::walkie::get_volume();
}

Status get_status()
{
    const ::walkie::Status source = ::walkie::get_status();
    Status status{};
    status.active = source.active;
    status.tx = source.tx;
    status.tx_level = source.tx_level;
    status.rx_level = source.rx_level;
    status.freq_mhz = source.freq_mhz;
    return status;
}

const char* last_error()
{
    return ::walkie::get_last_error();
}

} // namespace platform::ui::walkie
