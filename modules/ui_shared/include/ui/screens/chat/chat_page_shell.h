/**
 * @file chat_page_shell.h
 * @brief Shared chat page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace chat::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);
lv_obj_t* get_container();

} // namespace chat::ui::shell
