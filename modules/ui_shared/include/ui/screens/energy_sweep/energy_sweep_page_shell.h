/**
 * @file energy_sweep_page_shell.h
 * @brief Shared Energy Sweep page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

lv_obj_t* ui_energy_sweep_create(lv_obj_t* parent);
void ui_energy_sweep_enter(lv_obj_t* parent);
void ui_energy_sweep_exit(lv_obj_t* parent);

namespace energy_sweep::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);

} // namespace energy_sweep::ui::shell
