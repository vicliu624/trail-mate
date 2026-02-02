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
bool modal_is_open(const Modal& m);

#endif // GPS_MODAL_H
