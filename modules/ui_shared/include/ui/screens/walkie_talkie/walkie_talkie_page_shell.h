/**
 * @file walkie_talkie_page_shell.h
 * @brief Shared Walkie Talkie page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

void ui_walkie_talkie_enter(lv_obj_t* parent);
void ui_walkie_talkie_exit(lv_obj_t* parent);

namespace walkie_page::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);

} // namespace walkie_page::ui::shell
