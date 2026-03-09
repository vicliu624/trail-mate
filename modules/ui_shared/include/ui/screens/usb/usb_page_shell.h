/**
 * @file usb_page_shell.h
 * @brief Shared USB Mass Storage page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

#if defined(ARDUINO_USB_MODE)

void ui_usb_enter(lv_obj_t* parent);
void ui_usb_exit(lv_obj_t* parent);
bool ui_usb_is_active();

#endif

namespace usb_storage::ui::shell
{

using Host = ::ui::page::Host;

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);

} // namespace usb_storage::ui::shell
