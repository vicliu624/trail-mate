#include "platform/ui/usb_support_runtime.h"

namespace platform::ui::usb_support
{
namespace
{

constexpr const char* kUnsupportedMessage = "USB mass storage unsupported on Linux";

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

Status get_status()
{
    Status out{};
    out.active = false;
    out.stop_requested = false;
    out.message = kUnsupportedMessage;
    return out;
}

void prepare_mass_storage_mode()
{
}

void restore_mass_storage_mode()
{
}

} // namespace platform::ui::usb_support
