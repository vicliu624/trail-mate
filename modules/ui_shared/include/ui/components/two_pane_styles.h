#pragma once

#include "lvgl.h"

namespace ui::components::two_pane_styles
{

static constexpr uint32_t kSidePanelBg = 0xF6E6C6;
static constexpr uint32_t kMainPanelBg = 0xFAF0D8;
static constexpr uint32_t kBorder = 0xE7C98F;
static constexpr uint32_t kAccent = 0xEBA341;
static constexpr uint32_t kTextPrimary = 0x6B4A1E;
static constexpr uint32_t kTextMuted = 0x8A6A3A;

void init_once();

void apply_panel_side(lv_obj_t* obj);
void apply_panel_main(lv_obj_t* obj);
void apply_container_main(lv_obj_t* obj);

void apply_btn_basic(lv_obj_t* btn);
void apply_btn_filter(lv_obj_t* btn);
void apply_list_item(lv_obj_t* item);

void apply_label_primary(lv_obj_t* label);
void apply_label_muted(lv_obj_t* label);
void apply_label_accent(lv_obj_t* label);

} // namespace ui::components::two_pane_styles
