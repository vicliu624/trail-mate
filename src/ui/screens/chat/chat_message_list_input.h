/**
 * @file chat_message_list_input.h
 * @brief Input handling for ChatMessageListScreen (explicit layer)
 *
 * This module owns the navigation/focus policy for this screen.
 * For now it is intentionally minimal (no behavior change),
 * but it provides the official extension point for rotary/keys.
 */

#pragma once

#include "lvgl.h"

namespace chat::ui
{

class ChatMessageListScreen;

namespace message_list::input
{

enum class FocusColumn
{
    Filter = 0,
    List = 1
};

struct Binding
{
    ChatMessageListScreen* screen = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* prev_group = nullptr;
    FocusColumn col = FocusColumn::Filter;
    bool bound = false;
};

/**
 * @brief Initialize input handling for the screen
 */
void init(ChatMessageListScreen* screen, Binding* binding);

/**
 * @brief Clean up input handling
 */
void cleanup(Binding* binding);

/**
 * @brief Rebind focus after UI rebuild
 */
void on_ui_refreshed(Binding* binding);
void focus_filter(Binding* binding);
void focus_list(Binding* binding);

} // namespace message_list::input
} // namespace chat::ui
