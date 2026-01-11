/**
 * @file gps_modal.h
 * @brief Modal framework for popup management
 */

#ifndef GPS_MODAL_H
#define GPS_MODAL_H

#include "lvgl.h"

/**
 * Modal framework for popup management
 * Reduces popup code duplication and simplifies group/encoder management
 */
struct Modal {
    lv_obj_t* bg = nullptr;
    lv_obj_t* win = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* prev_default = nullptr;
    uint32_t close_ms = 0;
    
    bool is_open() const { return win != nullptr; }
};

// Modal management functions
bool modal_open(Modal& m, lv_obj_t* content_root, lv_group_t* focus_group);
void modal_close(Modal& m);

// Encoder/group management
lv_indev_t* get_encoder_indev();
void bind_encoder_to_group(lv_group_t *g);

#endif // GPS_MODAL_H
