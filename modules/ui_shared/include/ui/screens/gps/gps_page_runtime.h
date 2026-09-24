#pragma once

#include "ui/screens/gps/gps_page_shell.h"
#include "ui_presentation/map/map_overlay_snapshot.h"

#include <cstdint>

namespace gps::ui::runtime
{

bool is_available();
struct MapTarget
{
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    char name[97]{};
    void* overlay_context = nullptr;
    void (*append_overlays)(void*, ::ui::map::MapOverlaySnapshot&) = nullptr;
    void (*select_overlay)(void*, uint32_t) = nullptr;
};
// Target is borrowed until exit; caller owns its lifetime.
bool enter_target(const shell::Host* host, lv_obj_t* parent, const MapTarget& target);
void enter(const shell::Host* host,
           lv_obj_t* parent,
           shell::Projection projection = shell::Projection::Map);
void exit(lv_obj_t* parent);
void remember_gps_view_state();
bool restore_gps_view_state();
uint32_t selected_map_member_id();
bool load_map_track_file(const char* path, bool show_fail_toast);

} // namespace gps::ui::runtime
