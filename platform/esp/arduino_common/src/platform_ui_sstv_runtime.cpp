#include "platform/ui/sstv_runtime.h"

#include "board/BoardBase.h"

#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
#include "sstv/sstv_service.h"
#endif

namespace platform::ui::sstv
{

namespace
{

#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
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
#endif

} // namespace

bool is_supported()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return board.hasSstvAudioInput();
#else
    return false;
#endif
}

bool start()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::start();
#else
    return false;
#endif
}

void stop()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    ::sstv::stop();
#endif
}

bool is_active()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::is_active();
#else
    return false;
#endif
}

Status get_status()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    const ::sstv::Status source = ::sstv::get_status();
    Status status{};
    status.state = convert_state(source.state);
    status.line = source.line;
    status.progress = source.progress;
    status.audio_level = source.audio_level;
    status.has_image = source.has_image;
    return status;
#else
    return {};
#endif
}

const char* last_error()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::get_last_error();
#else
    return "SSTV disabled";
#endif
}

const char* last_saved_path()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::get_last_saved_path();
#else
    return nullptr;
#endif
}

const char* mode_name()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::get_mode_name();
#else
    return nullptr;
#endif
}

const uint16_t* framebuffer()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::get_framebuffer();
#else
    return nullptr;
#endif
}

uint16_t frame_width()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::frame_width();
#else
    return 0;
#endif
}

uint16_t frame_height()
{
#if !defined(TRAIL_MATE_ENABLE_SSTV) || TRAIL_MATE_ENABLE_SSTV
    return ::sstv::frame_height();
#else
    return 0;
#endif
}

} // namespace platform::ui::sstv
