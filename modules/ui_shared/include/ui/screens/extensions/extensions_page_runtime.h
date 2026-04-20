/**
 * @file extensions_page_runtime.h
 * @brief Shared Extensions page runtime
 */

#pragma once

#include "lvgl.h"
#include "ui/screens/extensions/extensions_page_shell.h"

namespace extensions::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace extensions::ui::runtime
