#include "platform/ui/sstv_runtime.h"

#include "boards/t_display_p4/board_profile.h"
#include "boards/tab5/tab5_board.h"
#include "sstv/sstv_service.h"

namespace
{

platform::ui::sstv::State convert_state(::sstv::State state)
{
    switch (state)
    {
    case ::sstv::State::Idle:
        return platform::ui::sstv::State::Idle;
    case ::sstv::State::Waiting:
        return platform::ui::sstv::State::Waiting;
    case ::sstv::State::Receiving:
        return platform::ui::sstv::State::Receiving;
    case ::sstv::State::Complete:
        return platform::ui::sstv::State::Complete;
    case ::sstv::State::Error:
        return platform::ui::sstv::State::Error;
    default:
        return platform::ui::sstv::State::Idle;
    }
}

bool board_supports_sstv()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasAudio() &&
           ::boards::tab5::Tab5Board::hasSdCard();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return platform::esp::boards::t_display_p4::kBoardProfile.has_audio &&
           platform::esp::boards::t_display_p4::kBoardProfile.has_sdcard;
#else
    return false;
#endif
}

} // namespace

namespace platform::ui::sstv
{

bool is_supported()
{
    return board_supports_sstv();
}

bool start()
{
    return is_supported() ? ::sstv::start() : false;
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
