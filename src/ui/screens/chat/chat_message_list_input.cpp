/**
 * @file chat_message_list_input.cpp
 * @brief Input handling for ChatMessageListScreen (explicit layer)
 *
 * Current policy (v0):
 * - Keep behavior identical to the original implementation.
 * - No event interception or focus changes.
 *
 * Future extensions:
 * - Rotary: up/down to change selected item
 * - Left/right: optional column switching if more focus zones appear
 * - Press: trigger current item / open conversation
 * - Long press: open context menu
 */

#include <Arduino.h>  // keep Arduino first if your build requires it
#include "chat_message_list_input.h"
#include "chat_message_list_components.h"

#define CHAT_INPUT_DEBUG 1
#if CHAT_INPUT_DEBUG
#define CHAT_INPUT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_INPUT_LOG(...)
#endif

namespace chat::ui::message_list::input {

static ChatMessageListScreen* s_screen = nullptr;

void init(ChatMessageListScreen* screen)
{
    s_screen = screen;

    // v0: No behavior change.
    // We do not attach LVGL event callbacks here yet,
    // because the original screen relies on:
    // - LV_EVENT_CLICKED on list items
    // - TopBar internal back callback
    //
    // This module is the explicit "input ownership" point for future work.

    CHAT_INPUT_LOG("[ChatMessageListInput] init (v0 no-op)\n");
}

void cleanup()
{
    // v0: No behavior change.
    s_screen = nullptr;
    CHAT_INPUT_LOG("[ChatMessageListInput] cleanup\n");
}

} // namespace chat::ui::input
