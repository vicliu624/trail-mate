#pragma once

#include "app/text_input.h"
#include <string>
#include <utility>

namespace trailmate::cardputer_zero::app
{

enum class InputKey
{
    Unknown = 0,
    Character,
    Backspace,
    Enter,
    Tab,
    Home,
    Next,
    Power,
    Fn,
    Ctrl,
    Alt,
    Shift,
    Left,
    Right,
    Up,
    Down,
    F1,
    TextEdit,
    CursorHome,
    CursorEnd,
};

struct InputEvent
{
    InputKey key{InputKey::Unknown};
    std::string label{};
    char text{'\0'};
    app::TextEdit edit{};
    bool shift = false;
    bool control = false;
    bool alt = false;
};

[[nodiscard]] inline InputEvent makeCharacterInput(char ch, std::string label = {})
{
    return InputEvent{InputKey::Character, std::move(label), ch};
}

} // namespace trailmate::cardputer_zero::app
