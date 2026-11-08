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

void set_ptt(bool pressed)
{
    ::walkie::set_ptt(pressed);
}

bool set_monitor_enabled(bool enabled)
{
    return ::walkie::set_monitor_enabled(enabled);
}

bool monitor_enabled()
{
    return ::walkie::is_monitor_enabled();
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
    status.monitor_enabled = source.monitor_enabled;
    status.tx_level = source.tx_level;
    status.rx_level = source.rx_level;
    status.freq_mhz = source.freq_mhz;
    return status;
}

const char* last_error()
{
    return ::walkie::get_last_error();
}

CapabilityStatus capability_status()
{
    if (!is_supported()) return {CapabilityState::Unsupported, "Walkie is not supported on this device."};
    const char* error = last_error();
    if (error && *error) return {CapabilityState::Error, error};
    return is_active() ? CapabilityStatus{CapabilityState::Available, "Walkie radio session is active."}
                       : CapabilityStatus{CapabilityState::Degraded, "Walkie radio is not active."};
}

} // namespace platform::ui::walkie
