/**
 * @file chat_message_list_input.h
 * @brief Input handling for ChatMessageListScreen
 */

#pragma once

#include "ui/components/two_pane_nav.h"

namespace chat
{
namespace ui
{

class ChatMessageListScreen;

namespace message_list
{
namespace input
{

using FocusColumn = ::ui::components::two_pane_nav::FocusColumn;
using Controller = ::ui::components::two_pane_nav::Controller;

void init(ChatMessageListScreen* screen, Controller* controller);
void cleanup(Controller* controller);
void on_ui_refreshed(Controller* controller);
void focus_filter(Controller* controller);
void focus_list(Controller* controller);

} // namespace input
} // namespace message_list
} // namespace ui
} // namespace chat
