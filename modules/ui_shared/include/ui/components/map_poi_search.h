#pragma once
#include "lvgl.h"
#include "platform/ui/map_search_runtime.h"

namespace ui::components::map_poi_search
{
using Selection = void (*)(const platform::ui::map_search::Result&, void*);
bool open(lv_obj_t* parent, double latitude, double longitude, Selection selection, void* context = nullptr);
void close();
} // namespace ui::components::map_poi_search
