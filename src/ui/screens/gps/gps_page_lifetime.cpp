#include "gps_page_lifetime.h"

#include "../../widgets/map/map_tiles.h"
#include "gps_modal.h"
#include "gps_page_map.h"
#include "gps_state.h"
#include "gps_tracker_overlay.h"
#include <algorithm>

namespace gps::ui::lifetime
{

namespace
{

void clear_modal_groups()
{
    if (g_gps_state.zoom_modal.group)
    {
        lv_group_del(g_gps_state.zoom_modal.group);
        g_gps_state.zoom_modal.group = nullptr;
    }
    if (g_gps_state.tracker_modal.group)
    {
        lv_group_del(g_gps_state.tracker_modal.group);
        g_gps_state.tracker_modal.group = nullptr;
    }
}

void detach_group_objs()
{
    lv_group_t* group = g_gps_state.app_group;
    if (!group)
    {
        return;
    }

    auto remove_if = [group](lv_obj_t* obj)
    {
        if (obj)
        {
            lv_group_remove_obj(obj);
        }
    };

    remove_if(g_gps_state.top_bar.back_btn);
    remove_if(g_gps_state.zoom);
    remove_if(g_gps_state.pos);
    remove_if(g_gps_state.pan_h);
    remove_if(g_gps_state.pan_v);
    remove_if(g_gps_state.tracker_btn);
    remove_if(g_gps_state.pan_h_indicator);
    remove_if(g_gps_state.pan_v_indicator);
    for (auto* btn : g_gps_state.member_btns)
    {
        remove_if(btn);
    }
}

void on_root_deleted(lv_event_t* e)
{
    (void)e;

    if (!g_gps_state.alive)
    {
        return;
    }

    g_gps_state.alive = false;
    g_gps_state.exiting = true;

    clear_timers();
    g_gps_state.timer = nullptr;
    g_gps_state.title_timer = nullptr;
    g_gps_state.toast_timer = nullptr;

    detach_group_objs();

    // Do not delete children here; the root delete will handle them.
    g_gps_state.loading_msgbox = nullptr;
    g_gps_state.toast_msgbox = nullptr;
    g_gps_state.popup_label = nullptr;

    // Close modals that are not children of the root container.
    if (g_gps_state.zoom_modal.is_open())
    {
        modal_close(g_gps_state.zoom_modal);
    }
    gps_tracker_cleanup();
    clear_modal_groups();

    ::cleanup_tiles(g_gps_state.tile_ctx);
    reset_title_status_cache();

    g_gps_state.map = nullptr;
    g_gps_state.header = nullptr;
    g_gps_state.page = nullptr;
    g_gps_state.panel = nullptr;
    g_gps_state.member_panel = nullptr;
    g_gps_state.member_btns.clear();
    g_gps_state.member_btn_ids.clear();
    g_gps_state.zoom = nullptr;
    g_gps_state.pos = nullptr;
    g_gps_state.pan_h = nullptr;
    g_gps_state.pan_v = nullptr;
    g_gps_state.tracker_btn = nullptr;
    g_gps_state.resolution_label = nullptr;
}

} // namespace

void mark_alive(lv_obj_t* root, lv_group_t* app_group)
{
    g_gps_state.root = root;
    g_gps_state.app_group = app_group;
    g_gps_state.alive = (root != nullptr);
}

bool is_alive()
{
    return g_gps_state.alive && g_gps_state.root != nullptr;
}

void bind_root_delete_hook()
{
    if (!g_gps_state.root || g_gps_state.delete_hook_bound)
    {
        return;
    }
    lv_obj_add_event_cb(g_gps_state.root, on_root_deleted, LV_EVENT_DELETE, nullptr);
    g_gps_state.delete_hook_bound = true;
}

lv_timer_t* add_timer(lv_timer_cb_t cb, uint32_t period_ms, void* user_data)
{
    if (!is_alive())
    {
        return nullptr;
    }
    lv_timer_t* timer = lv_timer_create(cb, period_ms, user_data);
    if (timer)
    {
        g_gps_state.timers.push_back(timer);
    }
    return timer;
}

void clear_timers()
{
    for (lv_timer_t* timer : g_gps_state.timers)
    {
        if (timer)
        {
            lv_timer_del(timer);
        }
    }
    g_gps_state.timers.clear();
}

void remove_timer(lv_timer_t* timer)
{
    if (!timer)
    {
        return;
    }

    auto& timers = g_gps_state.timers;
    timers.erase(std::remove(timers.begin(), timers.end(), timer), timers.end());
    lv_timer_del(timer);
}

} // namespace gps::ui::lifetime
