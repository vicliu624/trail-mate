/**
 * @file ui_status.cpp
 * @brief Global UI status indicators (top bar icons + menu badges)
 */

#include "ui/ui_status.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "ble/ble_manager.h"
#include "chat/usecase/chat_service.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "sys/clock.h"
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
#include "ui/screens/team/team_ui_store.h"
#endif
#include "ui/ui_theme.h"
#include "ui/theme/theme_registry.h"

#include <cstdio>
#include <string>

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
    bool wifi_enabled = false;
    bool team_active = false;
    bool ble_enabled = false;
    int unread = 0;
};

struct TeamSnapshotCache
{
    bool valid = false;
    uint32_t last_refresh_ms = 0;
    bool team_active = false;
    int team_unread = 0;
};

lv_timer_t* s_status_timer = nullptr;
lv_obj_t* s_menu_status_row = nullptr;
lv_obj_t* s_menu_route_icon = nullptr;
lv_obj_t* s_menu_tracker_icon = nullptr;
lv_obj_t* s_menu_gps_icon = nullptr;
lv_obj_t* s_menu_wifi_icon = nullptr;
lv_obj_t* s_menu_team_icon = nullptr;
lv_obj_t* s_menu_msg_icon = nullptr;
lv_obj_t* s_menu_ble_icon = nullptr;
lv_obj_t* s_chat_badge = nullptr;
lv_obj_t* s_chat_badge_label = nullptr;
TeamSnapshotCache s_team_cache;
constexpr uint32_t kTeamSnapshotRefreshMs = 5000;
bool s_menu_active = true;
std::string s_route_icon_path;
std::string s_tracker_icon_path;
std::string s_gps_icon_path;
std::string s_wifi_icon_path;
std::string s_team_icon_path;
std::string s_msg_icon_path;
std::string s_ble_icon_path;

bool obj_valid(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

lv_obj_t* holder_image(lv_obj_t* holder)
{
    return holder ? lv_obj_get_child(holder, 0) : nullptr;
}

lv_obj_t* holder_label(lv_obj_t* holder)
{
    return holder ? lv_obj_get_child(holder, 1) : nullptr;
}

const char* status_fallback_text(::ui::theme::AssetSlot slot)
{
    switch (slot)
    {
    case ::ui::theme::AssetSlot::StatusRoute:
        return "R";
    case ::ui::theme::AssetSlot::StatusTracker:
        return "T";
    case ::ui::theme::AssetSlot::StatusGps:
        return "G";
    case ::ui::theme::AssetSlot::StatusWifi:
        return "W";
    case ::ui::theme::AssetSlot::StatusTeam:
        return "TM";
    case ::ui::theme::AssetSlot::StatusMessage:
        return "M";
    case ::ui::theme::AssetSlot::StatusBle:
        return "B";
    default:
        return nullptr;
    }
}

void show_holder_image(lv_obj_t* holder, lv_obj_t* image_obj, lv_obj_t* label_obj)
{
    if (!obj_valid(holder) || !obj_valid(image_obj) || !obj_valid(label_obj))
    {
        return;
    }
    lv_obj_clear_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(image_obj);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_HIDDEN);
}

void show_holder_label(lv_obj_t* holder, lv_obj_t* image_obj, lv_obj_t* label_obj, const char* text)
{
    if (!obj_valid(holder) || !obj_valid(image_obj) || !obj_valid(label_obj))
    {
        return;
    }
    lv_label_set_text(label_obj, text ? text : "");
    lv_obj_set_style_text_color(label_obj, ::ui::theme::text(), 0);
    lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(label_obj);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_HIDDEN);
}

void refresh_team_cache(bool force = false)
{
#if defined(GAT562_NO_TEAM) && GAT562_NO_TEAM
    (void)force;
    s_team_cache.team_active = false;
    s_team_cache.team_unread = 0;
    s_team_cache.valid = true;
    s_team_cache.last_refresh_ms = sys::millis_now();
    return;
#else
    const uint32_t now = sys::millis_now();
    if (!force && s_team_cache.valid && (now - s_team_cache.last_refresh_ms) < kTeamSnapshotRefreshMs)
    {
        return;
    }

    team::ui::TeamUiSnapshot snap;
    if (team::ui::team_ui_get_store().load(snap))
    {
        s_team_cache.team_active = snap.in_team;
        s_team_cache.team_unread = static_cast<int>(snap.team_chat_unread);
    }
    else
    {
        s_team_cache.team_active = false;
        s_team_cache.team_unread = 0;
    }
    s_team_cache.valid = true;
    s_team_cache.last_refresh_ms = now;
#endif
}

StatusSnapshot collect_status()
{
    StatusSnapshot snap{};
    const auto& cfg = app::configFacade().getConfig();

    snap.route_active = cfg.route_enabled && (cfg.route_path[0] != '\0');
    snap.track_recording = platform::ui::tracker::is_recording();
    snap.gps_enabled = platform::ui::gps::is_enabled();
    snap.wifi_enabled = platform::ui::wifi::status().enabled;
    if (auto* ble = app::runtimeFacade().getBleManager())
    {
        snap.ble_enabled = ble->isEnabled();
    }

    refresh_team_cache();
    snap.team_active = s_team_cache.team_active;

    snap.unread = get_total_unread();
    return snap;
}

void apply_icon(lv_obj_t* icon,
                ::ui::theme::AssetSlot slot,
                std::string& backing_path,
                bool visible)
{
    if (!obj_valid(icon))
    {
        return;
    }

    lv_obj_t* image_obj = holder_image(icon);
    lv_obj_t* label_obj = holder_label(icon);
    if (!obj_valid(image_obj) || !obj_valid(label_obj))
    {
        if (visible)
        {
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    backing_path.clear();
    const bool has_external =
        ::ui::theme::resolve_asset_path(::ui::theme::asset_slot_id(slot), backing_path);
    if (has_external)
    {
        lv_image_set_src(image_obj, backing_path.c_str());
        show_holder_image(icon, image_obj, label_obj);
    }
    else
    {
        const lv_image_dsc_t* src = ::ui::theme::builtin_asset(slot);
        if (src)
        {
            lv_image_set_src(image_obj, src);
            show_holder_image(icon, image_obj, label_obj);
        }
        else if (const char* fallback = status_fallback_text(slot))
        {
            show_holder_label(icon, image_obj, label_obj, fallback);
        }
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

    apply_icon(s_menu_route_icon,
               ::ui::theme::AssetSlot::StatusRoute,
               s_route_icon_path,
               snap.route_active);
    apply_icon(s_menu_tracker_icon,
               ::ui::theme::AssetSlot::StatusTracker,
               s_tracker_icon_path,
               snap.track_recording);
    apply_icon(s_menu_gps_icon,
               ::ui::theme::AssetSlot::StatusGps,
               s_gps_icon_path,
               snap.gps_enabled);
    apply_icon(s_menu_wifi_icon,
               ::ui::theme::AssetSlot::StatusWifi,
               s_wifi_icon_path,
               snap.wifi_enabled);
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
    apply_icon(s_menu_team_icon,
               ::ui::theme::AssetSlot::StatusTeam,
               s_team_icon_path,
               snap.team_active);
#else
    apply_icon(s_menu_team_icon,
               ::ui::theme::AssetSlot::StatusTeam,
               s_team_icon_path,
               false);
#endif
    apply_icon(s_menu_msg_icon,
               ::ui::theme::AssetSlot::StatusMessage,
               s_msg_icon_path,
               snap.unread > 0);
    apply_icon(s_menu_ble_icon,
               ::ui::theme::AssetSlot::StatusBle,
               s_ble_icon_path,
               snap.ble_enabled);

    const bool any = snap.route_active || snap.track_recording || snap.gps_enabled ||
                     snap.wifi_enabled || snap.team_active || snap.ble_enabled ||
                     (snap.unread > 0);
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
    if (!s_menu_active)
    {
        return;
    }

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
                              lv_obj_t* wifi_icon,
                              lv_obj_t* team_icon,
                              lv_obj_t* msg_icon,
                              lv_obj_t* ble_icon)
{
    s_menu_status_row = row;
    s_menu_route_icon = route_icon;
    s_menu_tracker_icon = tracker_icon;
    s_menu_gps_icon = gps_icon;
    s_menu_wifi_icon = wifi_icon;
    s_menu_team_icon = team_icon;
    s_menu_msg_icon = msg_icon;
    s_menu_ble_icon = ble_icon;
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
    refresh_team_cache(true);
    status_timer_cb(nullptr);
}

void set_menu_active(bool active)
{
    s_menu_active = active;
    if (s_status_timer == nullptr)
    {
        return;
    }

    if (active)
    {
        lv_timer_resume(s_status_timer);
        status_timer_cb(nullptr);
    }
    else
    {
        lv_timer_pause(s_status_timer);
    }
}

int get_total_unread()
{
    chat::ChatService& chat = app::messagingFacade().getChatService();
    int unread = chat.getTotalUnread();
    refresh_team_cache();
    return unread + s_team_cache.team_unread;
}

} // namespace status
} // namespace ui
