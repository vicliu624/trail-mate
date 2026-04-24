#include "ui/ui_status.h"

namespace ui::status
{

void init()
{
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
    (void)row;
    (void)route_icon;
    (void)tracker_icon;
    (void)gps_icon;
    (void)wifi_icon;
    (void)team_icon;
    (void)msg_icon;
    (void)ble_icon;
}

void register_chat_badge(lv_obj_t* badge_bg, lv_obj_t* badge_label)
{
    (void)badge_bg;
    (void)badge_label;
}

void force_update()
{
}

int get_total_unread()
{
    return 0;
}

void set_menu_active(bool active)
{
    (void)active;
}

} // namespace ui::status
