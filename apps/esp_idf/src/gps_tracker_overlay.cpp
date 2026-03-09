#include "ui/screens/gps/gps_state.h"
#include "ui/screens/gps/gps_tracker_overlay.h"

#include <cstdio>

GPSPageState g_gps_state{};

void gps_tracker_open_modal()
{
    g_gps_state.tracker_overlay_active = true;
}

bool gps_tracker_load_file(const char* path, bool show_toast)
{
    (void)show_toast;
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        return false;
    }
    std::fclose(file);
    g_gps_state.tracker_file = path;
    g_gps_state.tracker_overlay_active = true;
    return true;
}

void gps_tracker_draw_event(lv_event_t* e)
{
    (void)e;
}

void gps_tracker_cleanup()
{
    g_gps_state.tracker_overlay_active = false;
    g_gps_state.tracker_file.clear();
    g_gps_state.tracker_points.clear();
    g_gps_state.tracker_screen_points.clear();
}
