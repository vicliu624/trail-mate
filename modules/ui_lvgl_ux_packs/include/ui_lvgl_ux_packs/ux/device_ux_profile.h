#pragma once

#include <cstdint>

namespace ui_lvgl_ux
{

enum class ScreenClass : uint8_t
{
    TinyStatus,
    CompactHandheld,
    DeckLandscape,
    TouchTablet,
    Desktop,
};

enum class InputModel : uint8_t
{
    ButtonsOnly,
    Keyboard,
    Touch,
    RotaryOrTrackball,
    DesktopKeyboardMouse,
};

enum class MapMode : uint8_t
{
    None,
    Compact,
    Full,
    Desktop,
};

enum class ChatMode : uint8_t
{
    None,
    StatusOnly,
    QuickMessage,
    Full,
};

struct DeviceUxProfile
{
    constexpr DeviceUxProfile() = default;

    constexpr DeviceUxProfile(const char* profile_id,
                              ScreenClass screen_class_value,
                              InputModel input_model_value,
                              MapMode map_mode_value,
                              ChatMode chat_mode_value,
                              bool team_actions,
                              bool gps_page,
                              bool key_verification_modal,
                              bool position_picker)
        : id(profile_id),
          screen_class(screen_class_value),
          input_model(input_model_value),
          map_mode(map_mode_value),
          chat_mode(chat_mode_value),
          supports_team_actions(team_actions),
          supports_gps_page(gps_page),
          supports_key_verification_modal(key_verification_modal),
          supports_position_picker(position_picker)
    {
    }

    const char* id = nullptr;
    ScreenClass screen_class = ScreenClass::CompactHandheld;
    InputModel input_model = InputModel::ButtonsOnly;
    MapMode map_mode = MapMode::Compact;
    ChatMode chat_mode = ChatMode::QuickMessage;

    bool supports_team_actions = false;
    bool supports_gps_page = true;
    bool supports_key_verification_modal = true;
    bool supports_position_picker = true;
};

} // namespace ui_lvgl_ux
