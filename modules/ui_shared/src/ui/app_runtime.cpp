#include "ui/app_runtime.h"
#include "ui/menu/menu_profile.h"

lv_obj_t* main_screen = nullptr;
lv_group_t* menu_g = nullptr;
lv_group_t* app_g = nullptr;

namespace
{
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
    if (main_screen == nullptr)
    {
        app->exit(nullptr);
        return;
    }
    lv_obj_t* parent = lv_obj_get_child(main_screen, 1);
    app->exit(parent);
    if (menu_g)
    {
        set_default_group(menu_g);
        lv_group_set_editing(menu_g, false);
    }
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
    ui_clear_active_app();
    set_default_group(menu_g);
    lv_tileview_set_tile_by_index(main_screen, 0, 0, transition_anim());
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
    if (s_pending_exit == app)
    {
        s_pending_exit = nullptr;
    }
    if (s_active_app && s_active_app != app)
    {
        s_active_app->exit(parent);
    }
    if (app)
    {
        app->enter(parent);
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
}

void ui_request_exit_to_menu()
{
    AppScreen* app = s_active_app;
    menu_show();
    if (app == nullptr)
    {
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
