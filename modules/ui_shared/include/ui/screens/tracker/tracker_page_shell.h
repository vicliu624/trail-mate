/**
 * @file tracker_page_shell.h
 * @brief Shared Tracker page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace tracker::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);

} // namespace tracker::ui::shell
