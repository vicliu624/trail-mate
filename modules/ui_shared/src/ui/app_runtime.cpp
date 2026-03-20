#include "ui/app_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"

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
bool s_overlay_active = false;

lv_anim_enable_t transition_anim()
{
    return ui::menu_profile::current().input_mode == ui::menu_profile::InputMode::TouchPrimary
               ? LV_ANIM_OFF
               : LV_ANIM_ON;
}

void show_menu_internal()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "show_menu_internal active=%s", s_active_app ? s_active_app->name() : "(null)");
#endif
    ui_clear_active_app();
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
        }
    }
    lv_group_set_default(group);
}

void menu_show()
{
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
        s_active_app->exit(parent);
    }
    if (app)
    {
#if defined(ESP_PLATFORM)
        ESP_LOGI(kTag, "enter app=%s", app->name());
#endif
        app->enter(parent);
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::App);
    }
    s_active_app = app;
}

void ui_exit_active_app(lv_obj_t* parent)
{
    if (s_active_app)
    {
        s_active_app->exit(parent);
    }
    s_active_app = nullptr;
    ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
}

void ui_request_exit_to_menu()
{
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

void ui_set_overlay_active(bool active)
{
    s_overlay_active = active;
}

bool ui_is_overlay_active()
{
    return s_overlay_active;
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
