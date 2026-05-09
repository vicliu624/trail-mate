#include "ui/screens/gps/gps_state.h"
#include "ui/screens/gps/gps_tracker_overlay.h"
#include "ui/widgets/system_notification.h"

#include <string>

GPSPageState g_gps_state{};

bool gps_tracker_load_file(const char* path, bool show_toast)
{
    (void)show_toast;
    g_gps_state.tracker_file = path ? path : "";
    g_gps_state.tracker_overlay_active = !g_gps_state.tracker_file.empty();
    return g_gps_state.tracker_overlay_active;
}

void gps_tracker_open_modal()
{
}

void gps_tracker_draw_event(lv_event_t* e)
{
    (void)e;
}

void gps_tracker_cleanup()
{
    g_gps_state.tracker_overlay_active = false;
    g_gps_state.tracker_file.clear();
}

void show_toast(const char* message, uint32_t duration_ms)
{
    ::ui::SystemNotification::show(message ? message : "", duration_ms);
}
