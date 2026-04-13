#include "ui/menu/menu_dashboard.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "platform/ui/orientation_runtime.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui/menu/menu_profile.h"
#include "ui/ui_theme.h"

namespace ui::menu::dashboard
{
namespace
{

// Tab5's 720x1280 menu can starve the LVGL task if we keep animating the
// dashboard aggressively while the menu grid is also visible. A slower cadence
// keeps the dashboard useful without continuously forcing large redraws.
constexpr uint32_t kDashboardTimerMs = 1000;

bool is_tab5_profile()
{
    return std::strcmp(ui::menu_profile::current().name, "tab5") == 0;
}

void refresh_dashboard(lv_timer_t* timer)
{
    (void)timer;
    auto& dashboard = dashboard_state();
    if (!is_tab5_profile() || dashboard.panel == nullptr)
    {
        return;
    }

    ++dashboard.tick;

    refresh_compass_widget();

    if ((dashboard.tick % 2U) == 1U)
    {
        refresh_recent_widget();
        refresh_gps_widget();
    }
    if ((dashboard.tick % 4U) == 1U)
    {
        refresh_mesh_widget();
    }
}

} // namespace

void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions& options)
{
    (void)options;
    if (!is_tab5_profile() || menu_panel == nullptr || grid_panel == nullptr)
    {
        return;
    }

    dashboard_reset();
    auto& dashboard = dashboard_state();
    const lv_coord_t screen_w = lv_display_get_physical_horizontal_resolution(nullptr);
    const lv_coord_t screen_h = lv_display_get_physical_vertical_resolution(nullptr);
    const lv_coord_t grid_w = lv_obj_get_width(grid_panel);
    const lv_coord_t right_gap = static_cast<lv_coord_t>(screen_w - grid_w - 16);
    const lv_coord_t top = 56;

    dashboard.dock_right = right_gap >= 240;
    dashboard.panel = lv_obj_create(menu_panel);
    lv_obj_set_style_bg_color(dashboard.panel, ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(dashboard.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dashboard.panel, 0, 0);
    lv_obj_set_style_pad_all(dashboard.panel, 0, 0);
    lv_obj_set_style_pad_row(dashboard.panel, 12, 0);
    lv_obj_set_style_pad_column(dashboard.panel, 12, 0);
    lv_obj_set_style_shadow_width(dashboard.panel, 0, 0);
    lv_obj_clear_flag(dashboard.panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t panel_w = 0;
    lv_coord_t panel_h = 0;
    if (dashboard.dock_right)
    {
        panel_w = right_gap;
        panel_h = std::max<lv_coord_t>(screen_h - top - 10, 220);
        lv_obj_set_size(dashboard.panel, panel_w, panel_h);
        lv_obj_align(dashboard.panel, LV_ALIGN_TOP_RIGHT, -8, top);
    }
    else
    {
        panel_w = std::max<lv_coord_t>(screen_w - 16, 320);
        panel_h = 240;
        lv_obj_set_size(dashboard.panel, panel_w, panel_h);
        lv_obj_align_to(dashboard.panel, grid_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);
    }

    dashboard.grid = lv_obj_create(dashboard.panel);
    lv_obj_set_size(dashboard.grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(dashboard.grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dashboard.grid, 0, 0);
    lv_obj_set_style_pad_all(dashboard.grid, 0, 0);
    lv_obj_set_style_pad_row(dashboard.grid, 12, 0);
    lv_obj_set_style_pad_column(dashboard.grid, 12, 0);
    lv_obj_set_flex_flow(dashboard.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(dashboard.grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(dashboard.grid, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t card_w = (panel_w - 12) / 2;
    const lv_coord_t card_h = dashboard.dock_right ? std::max<lv_coord_t>((panel_h - 12) / 2, 124) : 132;

    create_mesh_widget(dashboard.grid, card_w, card_h);
    create_gps_widget(dashboard.grid, card_w, card_h);
    create_recent_widget(dashboard.grid, card_w, card_h);
    create_compass_widget(dashboard.grid, card_w, card_h);

    dashboard.timer = lv_timer_create(refresh_dashboard, kDashboardTimerMs, nullptr);
    if (dashboard.timer != nullptr)
    {
        lv_timer_set_repeat_count(dashboard.timer, -1);
    }
    refresh_dashboard(nullptr);
}

void bringToFront()
{
    auto& dashboard = dashboard_state();
    if (dashboard.panel != nullptr)
    {
        lv_obj_move_foreground(dashboard.panel);
    }
}

void setActive(bool active)
{
    auto& dashboard = dashboard_state();
    if (dashboard.timer == nullptr)
    {
        return;
    }

    if (active)
    {
        lv_timer_resume(dashboard.timer);
    }
    else
    {
        lv_timer_pause(dashboard.timer);
    }
}

} // namespace ui::menu::dashboard

#endif
