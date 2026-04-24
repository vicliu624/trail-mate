#include "platform/ui/sstv_runtime.h"

namespace platform::ui::sstv
{
namespace
{

constexpr const char* kUnsupportedMessage = "SSTV unsupported on Linux";

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

Status get_status()
{
    return {};
}

const char* last_error()
{
    return kUnsupportedMessage;
}

const char* last_saved_path()
{
    return "";
}

const char* mode_name()
{
    return "";
}

const uint16_t* framebuffer()
{
    return nullptr;
}

uint16_t frame_width()
{
    return 0;
}

uint16_t frame_height()
{
    return 0;
}

} // namespace platform::ui::sstv
