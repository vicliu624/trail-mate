/**
 * @file team_page_shell.h
 * @brief Shared Team page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace sys
{
struct Event;
}

namespace team::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);
bool handle_event(void* user_data, sys::Event* event);

} // namespace team::ui::shell
