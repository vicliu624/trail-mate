/**
 * @file extensions_page_shell.h
 * @brief Shared Extensions page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace extensions::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);

} // namespace extensions::ui::shell
