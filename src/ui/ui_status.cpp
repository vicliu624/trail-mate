/**
 * @file ui_status.cpp
 * @brief Global UI status indicators (top bar icons + menu badges)
 */

#include "ui_status.h"

#include "../app/app_context.h"
#include "../gps/usecase/gps_service.h"
#include "../gps/usecase/track_recorder.h"
#include "screens/team/team_ui_store.h"

#include <cstdio>

extern "C"
{
    extern const lv_image_dsc_t gps_topbar;
    extern const lv_image_dsc_t message_topbar;
    extern const lv_image_dsc_t route_topbar;
    extern const lv_image_dsc_t team_topbar;
    extern const lv_image_dsc_t tracker_topbar;
}

namespace ui
{
namespace status
{

namespace
{
struct StatusSnapshot
{
    bool route_active = false;
    bool track_recording = false;
    bool gps_enabled = false;
    bool team_active = false;
    int unread = 0;
};

lv_timer_t* s_status_timer = nullptr;
lv_obj_t* s_menu_status_row = nullptr;
lv_obj_t* s_menu_route_icon = nullptr;
lv_obj_t* s_menu_tracker_icon = nullptr;
lv_obj_t* s_menu_gps_icon = nullptr;
lv_obj_t* s_menu_team_icon = nullptr;
lv_obj_t* s_menu_msg_icon = nullptr;
lv_obj_t* s_chat_badge = nullptr;
lv_obj_t* s_chat_badge_label = nullptr;

bool obj_valid(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

int get_total_unread()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    chat::ChatService& chat = app_ctx.getChatService();
    int unread = chat.getTotalUnread();
    team::ui::TeamUiSnapshot snap;
    if (team::ui::team_ui_get_store().load(snap))
    {
        unread += static_cast<int>(snap.team_chat_unread);
    }
    return unread;
}

StatusSnapshot collect_status()
{
    StatusSnapshot snap{};
    app::AppContext& app_ctx = app::AppContext::getInstance();
    const auto& cfg = app_ctx.getConfig();

    snap.route_active = cfg.route_enabled && (cfg.route_path[0] != '\0');
    snap.track_recording = gps::TrackRecorder::getInstance().isRecording();
    snap.gps_enabled = gps::GpsService::getInstance().isEnabled();

    team::ui::TeamUiSnapshot team_snap;
    if (team::ui::team_ui_get_store().load(team_snap))
    {
        snap.team_active = team_snap.in_team;
    }

    snap.unread = get_total_unread();
    return snap;
}

void apply_icon(lv_obj_t* icon, const lv_image_dsc_t* src, bool visible)
{
    if (!obj_valid(icon))
    {
        return;
    }
    if (src)
    {
        lv_image_set_src(icon, src);
    }
    if (visible)
    {
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
}

void apply_menu_icons(const StatusSnapshot& snap)
{
    if (!obj_valid(s_menu_status_row))
    {
        return;
    }

    apply_icon(s_menu_route_icon, &route_topbar, snap.route_active);
    apply_icon(s_menu_tracker_icon, &tracker_topbar, snap.track_recording);
    apply_icon(s_menu_gps_icon, &gps_topbar, snap.gps_enabled);
    apply_icon(s_menu_team_icon, &team_topbar, snap.team_active);
    apply_icon(s_menu_msg_icon, &message_topbar, snap.unread > 0);

    const bool any =
        snap.route_active || snap.track_recording || snap.gps_enabled || snap.team_active || (snap.unread > 0);
    if (any)
    {
        lv_obj_clear_flag(s_menu_status_row, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_menu_status_row, LV_OBJ_FLAG_HIDDEN);
    }
}

void apply_menu_badge(const StatusSnapshot& snap)
{
    if (!obj_valid(s_chat_badge) || !obj_valid(s_chat_badge_label))
    {
        return;
    }
    if (snap.unread <= 0)
    {
        lv_obj_add_flag(s_chat_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", snap.unread);
    lv_label_set_text(s_chat_badge_label, buf);
    lv_obj_clear_flag(s_chat_badge, LV_OBJ_FLAG_HIDDEN);
}

void status_timer_cb(lv_timer_t* /*timer*/)
{
    StatusSnapshot snap = collect_status();

    apply_menu_icons(snap);
    apply_menu_badge(snap);
}
} // namespace

void init()
{
    if (s_status_timer)
    {
        return;
    }
    s_status_timer = lv_timer_create(status_timer_cb, 1000, nullptr);
    if (s_status_timer)
    {
        lv_timer_set_repeat_count(s_status_timer, -1);
    }
    status_timer_cb(nullptr);
}

void register_menu_status_row(lv_obj_t* row,
                              lv_obj_t* route_icon,
                              lv_obj_t* tracker_icon,
                              lv_obj_t* gps_icon,
                              lv_obj_t* team_icon,
                              lv_obj_t* msg_icon)
{
    s_menu_status_row = row;
    s_menu_route_icon = route_icon;
    s_menu_tracker_icon = tracker_icon;
    s_menu_gps_icon = gps_icon;
    s_menu_team_icon = team_icon;
    s_menu_msg_icon = msg_icon;
    status_timer_cb(nullptr);
}

void register_chat_badge(lv_obj_t* badge_bg, lv_obj_t* badge_label)
{
    s_chat_badge = badge_bg;
    s_chat_badge_label = badge_label;
    status_timer_cb(nullptr);
}

void force_update()
{
    status_timer_cb(nullptr);
}

} // namespace status
} // namespace ui
