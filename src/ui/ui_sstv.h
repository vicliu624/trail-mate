#pragma once

#include "lvgl.h"

enum SstvState
{
    SSTV_STATE_WAITING = 0,
    SSTV_STATE_RECEIVING = 1,
    SSTV_STATE_COMPLETE = 2,
};

lv_obj_t* ui_sstv_create(lv_obj_t* parent);
void ui_sstv_enter(lv_obj_t* parent);
void ui_sstv_exit(lv_obj_t* parent);

void ui_sstv_set_state(SstvState state);
void ui_sstv_set_mode(const char* mode_str);
void ui_sstv_set_audio_level(float level_0_1);
void ui_sstv_set_progress(float p_0_1);
void ui_sstv_set_image(const void* img_src_or_lv_img_dsc);
