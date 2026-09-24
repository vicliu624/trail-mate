#pragma once
#include "lvgl.h"
#include "ui/page/page_host.h"
#include "ui_presentation/geocaching/geocaching_source.h"

namespace geocaching::ui::shell
{
void bind(::ui::geocaching::Source* source);
void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);
} // namespace geocaching::ui::shell
