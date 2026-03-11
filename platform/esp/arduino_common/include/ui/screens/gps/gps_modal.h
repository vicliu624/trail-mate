#ifndef GPS_MODAL_H
#define GPS_MODAL_H

#include "lvgl.h"

typedef struct
{
    lv_obj_t* bg;
    lv_obj_t* win;
    lv_group_t* group;
    lv_indev_t* indev;
    lv_group_t* prev_default;
    uint32_t close_ms;
    bool open;

    bool is_open() const { return open; }
} Modal;

lv_indev_t* get_encoder_indev();
void bind_encoder_to_group(lv_group_t* g);

bool modal_open(Modal& m, lv_obj_t* content_root, lv_group_t* focus_group);
void modal_close(Modal& m);
void modal_set_size(Modal& m, lv_coord_t w, lv_coord_t h);
void modal_set_requested_size(Modal& m, lv_coord_t requested_w, lv_coord_t requested_h);
bool modal_is_open(const Modal& m);

bool modal_uses_touch_layout();
lv_coord_t modal_resolve_title_height();
lv_coord_t modal_resolve_action_button_height();
lv_obj_t* modal_create_touch_title_bar(lv_obj_t* win, const char* title);
lv_obj_t* modal_create_touch_content_area(lv_obj_t* win, lv_coord_t title_height);
lv_obj_t* modal_create_touch_action_button(lv_obj_t* parent,
                                          const char* text,
                                          lv_event_cb_t cb,
                                          void* user_data,
                                          lv_coord_t width);

#endif // GPS_MODAL_H
