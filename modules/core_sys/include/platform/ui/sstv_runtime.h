#pragma once

#include <cstdint>

namespace platform::ui::sstv
{

enum class State : uint8_t
{
    Idle = 0,
    Waiting,
    Receiving,
    Complete,
    Error,
};

struct Status
{
    State state = State::Idle;
    uint16_t line = 0;
    float progress = 0.0f;
    float audio_level = 0.0f;
    bool has_image = false;
};

bool is_supported();
bool start();
void stop();
bool is_active();
Status get_status();
const char* last_error();
const char* last_saved_path();
const char* mode_name();
const uint16_t* framebuffer();
uint16_t frame_width();
uint16_t frame_height();

} // namespace platform::ui::sstv
