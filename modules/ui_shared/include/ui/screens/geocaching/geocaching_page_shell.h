#pragma once
#include "lvgl.h"
#include "ui/page/page_host.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include "ui_presentation/map/map_overlay_snapshot.h"

namespace geocaching::ui::shell
{
void bind(::ui::geocaching::Source* source);
void beginMapOverlays();
bool pollMapOverlays(double latitude, double longitude, uint8_t zoom);
void appendMapOverlays(::ui::map::MapOverlaySnapshot& out, double latitude, double longitude, uint8_t zoom);
void endMapOverlays();
void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);
} // namespace geocaching::ui::shell
