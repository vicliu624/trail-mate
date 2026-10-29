#pragma once
#include "lvgl.h"
namespace extensions::ui::tmap_creator
{
void open(lv_obj_t* parent, void (*closed)(void*), void* context);
void shutdown();
} // namespace extensions::ui::tmap_creator
