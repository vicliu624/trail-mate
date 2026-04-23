/**
 * @file chat_message_list_input.h
 * @brief Input handling for ChatMessageListScreen
 */

#pragma once

#include "ui/presentation/directory_browser_nav.h"

namespace chat
{
namespace ui
{

class ChatMessageListScreen;

namespace message_list
{
namespace input
{

using FocusRegion = ::ui::presentation::directory_browser_nav::FocusRegion;
using Controller = ::ui::presentation::directory_browser_nav::Controller;

void init(ChatMessageListScreen* screen, Controller* controller);
void cleanup(Controller* controller);
void on_ui_refreshed(Controller* controller);
void focus_selector(Controller* controller);
void focus_content(Controller* controller);

} // namespace input
} // namespace message_list
} // namespace ui
} // namespace chat
