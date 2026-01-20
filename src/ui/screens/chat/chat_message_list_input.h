/**
 * @file chat_message_list_input.h
 * @brief Input handling for ChatMessageListScreen (explicit layer)
 *
 * This module owns the navigation/focus policy for this screen.
 * For now it is intentionally minimal (no behavior change),
 * but it provides the official extension point for rotary/keys.
 */

#pragma once

namespace chat::ui {

class ChatMessageListScreen;

namespace message_list::input {

/**
 * @brief Initialize input handling for the screen
 *
 * Behavior today:
 * - No additional event handlers are installed.
 * - No focus policy is changed.
 * - This function only establishes an explicit extension point.
 */
void init(ChatMessageListScreen* screen);

/**
 * @brief Clean up input handling
 *
 * Behavior today:
 * - No-op (placeholder for future unregistration if needed).
 */
void cleanup();

/**
 * @brief Rebind focus after UI rebuild
 */
void on_ui_refreshed();
void focus_filter();
void focus_list();

} // namespace message_list::input
} // namespace chat::ui
