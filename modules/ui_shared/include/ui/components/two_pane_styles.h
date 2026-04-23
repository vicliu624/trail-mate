#pragma once

#include "lvgl.h"
#include "ui/ui_theme.h"

namespace ui::components::two_pane_styles
{

// Style helpers for the builtin split-sidebar directory presentation.
// Public semantics live in ui::presentation contracts; these names describe a
// concrete presentation implementation.

inline uint32_t side_panel_bg_hex() { return lv_color_to_u32(::ui::theme::surface_alt()); }
inline uint32_t main_panel_bg_hex() { return lv_color_to_u32(::ui::theme::surface()); }
inline uint32_t border_hex() { return lv_color_to_u32(::ui::theme::border()); }
inline uint32_t accent_hex() { return lv_color_to_u32(::ui::theme::accent()); }
inline uint32_t text_primary_hex() { return lv_color_to_u32(::ui::theme::text()); }
inline uint32_t text_muted_hex() { return lv_color_to_u32(::ui::theme::text_muted()); }

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
