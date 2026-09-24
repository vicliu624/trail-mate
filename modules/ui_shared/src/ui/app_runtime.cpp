#include "ui/app_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

lv_obj_t* main_screen = nullptr;
lv_group_t* menu_g = nullptr;
lv_group_t* app_g = nullptr;

namespace
{
#if defined(ESP_PLATFORM)
constexpr const char* kTag = "ui-app-runtime";
#endif

AppScreen* s_active_app = nullptr;
AppScreen* s_pending_exit = nullptr;
lv_timer_t* s_exit_timer = nullptr;
lv_timer_t* s_rebuild_timer = nullptr;
bool s_overlay_active = false;
AppScreen* s_interruption_app = nullptr;
AppScreen* s_interrupted_app = nullptr;
bool s_interrupted_was_menu = false;

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
struct AppEdgeSwipeState
{
    bool pressed = false;
    bool armed = false;
    bool triggered = false;
    lv_point_t start = {0, 0};
    lv_point_t last = {0, 0};
};

AppEdgeSwipeState s_app_edge_swipe;

constexpr lv_coord_t kAppBackEdgeBandMin = 36;
constexpr lv_coord_t kAppBackEdgeBandMax = 76;
constexpr lv_coord_t kAppBackSwipeMin = 88;
constexpr lv_coord_t kAppBackSwipeMax = 160;
constexpr lv_coord_t kAppBackSwipeSlop = 28;

void reset_app_edge_swipe()
{
    s_app_edge_swipe = {};
}

lv_coord_t active_view_height()
{
    if (main_screen != nullptr)
    {
        const lv_coord_t height = lv_obj_get_height(main_screen);
        if (height > 0)
        {
            return height;
        }
    }

    const lv_coord_t height = lv_display_get_vertical_resolution(nullptr);
    return height > 0 ? height : static_cast<lv_coord_t>(540);
}

lv_coord_t bottom_edge_band(lv_coord_t height)
{
    return std::min(kAppBackEdgeBandMax, std::max(kAppBackEdgeBandMin, height / 10));
}

lv_coord_t back_swipe_threshold(lv_coord_t height)
{
    return std::min(kAppBackSwipeMax, std::max(kAppBackSwipeMin, height / 6));
}

bool app_edge_swipe_enabled()
{
    return ui::menu_runtime::currentScene() == ui::menu_runtime::Scene::App &&
           ui_get_active_app() != nullptr &&
           !ui_is_overlay_active();
}

bool starts_in_bottom_edge(const lv_point_t& point)
{
    const lv_coord_t height = active_view_height();
    return point.y >= height - bottom_edge_band(height);
}

bool crosses_back_swipe_threshold(const lv_point_t& point)
{
    const lv_coord_t height = active_view_height();
    const int dx = point.x - s_app_edge_swipe.start.x;
    const int dy = point.y - s_app_edge_swipe.start.y;
    return dy <= -back_swipe_threshold(height) &&
           std::abs(dy) > std::abs(dx) + kAppBackSwipeSlop;
}

void trigger_app_edge_back(lv_indev_t* indev)
{
    if (s_app_edge_swipe.triggered)
    {
        return;
    }
    s_app_edge_swipe.triggered = true;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "bottom edge swipe -> exit to menu app=%s",
             s_active_app ? s_active_app->name() : "(null)");
#endif
    ui_request_exit_to_menu();
    if (indev != nullptr)
    {
        lv_indev_wait_release(indev);
        lv_indev_stop_processing(indev);
    }
}

void app_edge_swipe_indev_cb(lv_event_t* e)
{
    lv_indev_t* indev = static_cast<lv_indev_t*>(lv_event_get_current_target(e));
    if (indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
    {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
        code != LV_EVENT_GESTURE)
    {
        return;
    }

    if (!app_edge_swipe_enabled())
    {
        reset_app_edge_swipe();
        return;
    }

    lv_point_t point{};
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED)
    {
        reset_app_edge_swipe();
        s_app_edge_swipe.pressed = true;
        s_app_edge_swipe.armed = starts_in_bottom_edge(point);
        s_app_edge_swipe.start = point;
        s_app_edge_swipe.last = point;
        return;
    }

    if (!s_app_edge_swipe.pressed || !s_app_edge_swipe.armed)
    {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        {
            reset_app_edge_swipe();
        }
        return;
    }

    s_app_edge_swipe.last = point;
    if (code == LV_EVENT_GESTURE)
    {
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_TOP && crosses_back_swipe_threshold(point))
        {
            trigger_app_edge_back(indev);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING && crosses_back_swipe_threshold(point))
    {
        trigger_app_edge_back(indev);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        if (code == LV_EVENT_RELEASED && crosses_back_swipe_threshold(point))
        {
            trigger_app_edge_back(indev);
            return;
        }
        reset_app_edge_swipe();
    }
}

void install_app_edge_swipe(lv_indev_t* indev)
{
    if (indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
    {
        return;
    }

    lv_indev_remove_event_cb_with_user_data(indev, app_edge_swipe_indev_cb, nullptr);
    lv_indev_add_event_cb(indev, app_edge_swipe_indev_cb, LV_EVENT_PRESSED, nullptr);
    lv_indev_add_event_cb(indev, app_edge_swipe_indev_cb, LV_EVENT_PRESSING, nullptr);
    lv_indev_add_event_cb(indev, app_edge_swipe_indev_cb, LV_EVENT_RELEASED, nullptr);
    lv_indev_add_event_cb(indev, app_edge_swipe_indev_cb, LV_EVENT_PRESS_LOST, nullptr);
    lv_indev_add_event_cb(indev, app_edge_swipe_indev_cb, LV_EVENT_GESTURE, nullptr);
}
#endif

lv_anim_enable_t transition_anim()
{
    return ui::menu_profile::current().transition_animation ? LV_ANIM_ON : LV_ANIM_OFF;
}

void show_menu_internal()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "show_menu_internal active=%s", s_active_app ? s_active_app->name() : "(null)");
#endif
    std::printf("[UI][Lifecycle] menu visible=1 active=%s scene=menu\n",
                s_active_app ? s_active_app->name() : "<none>");
    set_default_group(menu_g);
    ui::menu_layout::setMenuVisible(true);
    ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
    lv_tileview_set_tile_by_index(main_screen, 0, 0, transition_anim());
}

uint32_t child_count(lv_obj_t* obj)
{
    if (obj == nullptr)
    {
        return 0;
    }
#if LVGL_VERSION_MAJOR >= 9
    return lv_obj_get_child_count(obj);
#else
    return static_cast<uint32_t>(lv_obj_get_child_cnt(obj));
#endif
}

uint32_t exit_delay_ms()
{
    return ui::menu_profile::current().input_mode == ui::menu_profile::InputMode::TouchPrimary ? 16U : 120U;
}

void exit_to_menu_timer_cb(lv_timer_t* timer)
{
#if LVGL_VERSION_MAJOR >= 9
    auto* app = static_cast<AppScreen*>(timer ? lv_timer_get_user_data(timer) : nullptr);
#else
    auto* app = static_cast<AppScreen*>(timer ? timer->user_data : nullptr);
#endif
    if (timer)
    {
        lv_timer_del(timer);
    }
    s_exit_timer = nullptr;
    if (!app || s_pending_exit != app)
    {
        return;
    }
    s_pending_exit = nullptr;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "exit_to_menu_timer_cb begin app=%s", app->name());
#endif
    if (main_screen == nullptr)
    {
        app->exit(nullptr);
        s_active_app = nullptr;
        return;
    }
    lv_obj_t* parent = lv_obj_get_child(main_screen, 1);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag,
             "exit_to_menu_timer_cb parent=%p child_count_before=%lu",
             parent,
             static_cast<unsigned long>(child_count(parent)));
#endif
    std::printf("[UI][Lifecycle] app exit name=%s reason=to_menu\n",
                app->name());
    app->exit(parent);
    if (parent != nullptr)
    {
        const uint32_t leaked_children = child_count(parent);
        if (leaked_children > 0)
        {
#if defined(ESP_PLATFORM)
            ESP_LOGW(kTag,
                     "exit_to_menu_timer_cb cleaning leaked app_panel children=%lu",
                     static_cast<unsigned long>(leaked_children));
#endif
            lv_obj_clean(parent);
        }
    }
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag,
             "exit_to_menu_timer_cb parent=%p child_count_after=%lu",
             parent,
             static_cast<unsigned long>(child_count(parent)));
#endif
    s_active_app = nullptr;
    show_menu_internal();
    if (menu_g)
    {
        lv_group_set_editing(menu_g, false);
    }
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "exit_to_menu_timer_cb complete");
#endif
}

void rebuild_active_app_timer_cb(lv_timer_t* timer)
{
#if LVGL_VERSION_MAJOR >= 9
    auto* app = static_cast<AppScreen*>(timer ? lv_timer_get_user_data(timer) : nullptr);
#else
    auto* app = static_cast<AppScreen*>(timer ? timer->user_data : nullptr);
#endif
    if (timer)
    {
        lv_timer_del(timer);
    }
    s_rebuild_timer = nullptr;
    if (app == nullptr || app != s_active_app || main_screen == nullptr)
    {
        return;
    }

    lv_obj_t* parent = lv_obj_get_child(main_screen, 1);
    if (parent == nullptr)
    {
        return;
    }

#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag,
             "[UI][Txn] active_rebuild begin app=%s parent=%p children_before=%lu",
             app->name(),
             parent,
             static_cast<unsigned long>(child_count(parent)));
#endif
    app->exit(parent);
    if (parent != nullptr && child_count(parent) > 0)
    {
        lv_obj_clean(parent);
    }
    app->enter(parent);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag,
             "[UI][Txn] active_rebuild complete app=%s parent=%p children_after=%lu",
             app->name(),
             parent,
             static_cast<unsigned long>(child_count(parent)));
#endif
}
} // namespace

void set_default_group(lv_group_t* group)
{
    lv_indev_t* cur_drv = nullptr;
    for (;;)
    {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv)
        {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_set_group(cur_drv, group);
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
            install_app_edge_swipe(cur_drv);
#endif
        }
    }
    lv_group_set_default(group);
}

void menu_show()
{
    if (s_interruption_app != nullptr)
    {
        return;
    }

    // Showing the menu while an app is active is a lifecycle transition, not
    // a visual shortcut. The app owns timers, LVGL children, and device
    // leases that must be released by its exit callback before the menu can
    // become the active scene.
    if (s_active_app != nullptr)
    {
        ui_request_exit_to_menu();
        return;
    }

    show_menu_internal();
}

AppScreen* ui_get_active_app()
{
    return s_active_app;
}

void ui_clear_active_app()
{
    s_active_app = nullptr;
}

void ui_switch_to_app(AppScreen* app, lv_obj_t* parent)
{
    if (s_interruption_app != nullptr && app != s_interruption_app)
    {
        return;
    }
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag,
             "ui_switch_to_app from=%s to=%s parent=%d",
             s_active_app ? s_active_app->name() : "(null)",
             app ? app->name() : "(null)",
             parent ? 1 : 0);
#endif
    if (s_pending_exit == app)
    {
        s_pending_exit = nullptr;
    }
    if (s_active_app && s_active_app != app)
    {
#if defined(ESP_PLATFORM)
        ESP_LOGI(kTag, "exiting previous app=%s", s_active_app->name());
#endif
        std::printf("[UI][Lifecycle] app exit name=%s reason=switch\n",
                    s_active_app->name());
        s_active_app->exit(parent);
    }
    if (app)
    {
#if defined(ESP_PLATFORM)
        ESP_LOGI(kTag, "enter app=%s", app->name());
#endif
        std::printf("[UI][Lifecycle] app enter from=%s to=%s parent=%d\n",
                    s_active_app ? s_active_app->name() : "<none>",
                    app->name(),
                    parent ? 1 : 0);
        app->enter(parent);
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::App);
    }
    s_active_app = app;
}

void ui_exit_active_app(lv_obj_t* parent)
{
    if (s_interruption_app != nullptr)
    {
        return;
    }
    if (s_active_app)
    {
        std::printf("[UI][Lifecycle] app exit name=%s reason=explicit\n",
                    s_active_app->name());
        s_active_app->exit(parent);
    }
    s_active_app = nullptr;
    ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
}

bool ui_present_interruption_app(AppScreen* app, lv_obj_t* parent)
{
    if (!app || !parent)
    {
        return false;
    }
    if (s_interruption_app != nullptr)
    {
        return s_interruption_app == app && s_active_app == app;
    }

    const bool exit_to_menu_was_pending =
        s_pending_exit != nullptr && s_pending_exit == s_active_app;
    s_interrupted_app = s_active_app;
    s_interrupted_was_menu =
        ui::menu_runtime::currentScene() == ui::menu_runtime::Scene::Menu ||
        exit_to_menu_was_pending;
    if (s_exit_timer)
    {
        lv_timer_del(s_exit_timer);
        s_exit_timer = nullptr;
    }
    if (s_rebuild_timer)
    {
        lv_timer_del(s_rebuild_timer);
        s_rebuild_timer = nullptr;
    }
    s_pending_exit = nullptr;
    s_interruption_app = app;
    ui_set_overlay_active(true);
    ui::menu_layout::setMenuVisible(false);
    if (main_screen != nullptr)
    {
        lv_tileview_set_tile_by_index(main_screen, 0, 1, LV_ANIM_OFF);
    }
    ui_switch_to_app(app, parent);
    return s_active_app == app;
}

void ui_dismiss_interruption_app(lv_obj_t* parent)
{
    AppScreen* interruption = s_interruption_app;
    if (!interruption)
    {
        return;
    }

    AppScreen* interrupted = s_interrupted_app;
    const bool restore_menu = s_interrupted_was_menu || interrupted == nullptr;
    s_interruption_app = nullptr;
    s_interrupted_app = nullptr;
    s_interrupted_was_menu = false;
    ui_set_overlay_active(false);

    if (s_active_app == interruption)
    {
        std::printf("[UI][Lifecycle] app exit name=%s reason=interruption_complete\n",
                    interruption->name());
        interruption->exit(parent);
        s_active_app = nullptr;
    }

    if (!restore_menu && interrupted)
    {
        ui::menu_layout::setMenuVisible(false);
        ui_switch_to_app(interrupted, parent);
        return;
    }
    show_menu_internal();
}

bool ui_is_interruption_app_active()
{
    return s_interruption_app != nullptr;
}

void ui_request_exit_to_menu()
{
    if (s_interruption_app != nullptr)
    {
        return;
    }
    AppScreen* app = s_active_app;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "ui_request_exit_to_menu app=%s", app ? app->name() : "(null)");
#endif
    if (app == nullptr)
    {
        menu_show();
        return;
    }
    if (s_pending_exit == app)
    {
        return;
    }
    s_pending_exit = app;
    if (s_exit_timer)
    {
        lv_timer_del(s_exit_timer);
        s_exit_timer = nullptr;
    }
    s_exit_timer = lv_timer_create(exit_to_menu_timer_cb, exit_delay_ms(), app);
    if (s_exit_timer)
    {
        lv_timer_set_repeat_count(s_exit_timer, 1);
    }
}

void ui_request_rebuild_active_app()
{
    AppScreen* app = s_active_app;
    if (app == nullptr || s_pending_exit == app)
    {
        return;
    }
    if (s_rebuild_timer)
    {
        lv_timer_del(s_rebuild_timer);
        s_rebuild_timer = nullptr;
    }
    s_rebuild_timer = lv_timer_create(rebuild_active_app_timer_cb, 1, app);
    if (s_rebuild_timer)
    {
        lv_timer_set_repeat_count(s_rebuild_timer, 1);
    }
}

void ui_set_overlay_active(bool active)
{
    s_overlay_active = active;
}

bool ui_is_overlay_active()
{
    return s_overlay_active;
}

bool ui_is_transition_pending()
{
    return s_pending_exit != nullptr || s_exit_timer != nullptr || s_rebuild_timer != nullptr;
}

lv_obj_t* create_menu(lv_obj_t* parent, lv_event_cb_t event_cb)
{
    lv_obj_t* menu = lv_menu_create(parent);
    lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
    lv_obj_add_event_cb(menu, event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_center(menu);
    return menu;
}
