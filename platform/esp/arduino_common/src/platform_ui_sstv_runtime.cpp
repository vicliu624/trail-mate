#include "platform/ui/sstv_runtime.h"

#include "sstv/sstv_service.h"

namespace platform::ui::sstv
{

namespace
{

State convert_state(::sstv::State state)
{
    switch (state)
    {
    case ::sstv::State::Idle:
        return State::Idle;
    case ::sstv::State::Waiting:
        return State::Waiting;
    case ::sstv::State::Receiving:
        return State::Receiving;
    case ::sstv::State::Complete:
        return State::Complete;
    case ::sstv::State::Error:
        return State::Error;
    default:
        return State::Idle;
    }
}

} // namespace

bool is_supported()
{
    return true;
}

bool start()
{
    return ::sstv::start();
}

void stop()
{
    ::sstv::stop();
}

bool is_active()
{
    return ::sstv::is_active();
}

Status get_status()
{
    const ::sstv::Status source = ::sstv::get_status();
    Status status{};
    status.state = convert_state(source.state);
    status.line = source.line;
    status.progress = source.progress;
    status.audio_level = source.audio_level;
    status.has_image = source.has_image;
    return status;
}

const char* last_error()
{
    return ::sstv::get_last_error();
}

const char* last_saved_path()
{
    return ::sstv::get_last_saved_path();
}

const char* mode_name()
{
    return ::sstv::get_mode_name();
}

const uint16_t* framebuffer()
{
    return ::sstv::get_framebuffer();
}

uint16_t frame_width()
{
    return ::sstv::frame_width();
}

uint16_t frame_height()
{
    return ::sstv::frame_height();
}

} // namespace platform::ui::sstv
