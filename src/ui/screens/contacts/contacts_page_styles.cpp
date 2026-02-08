#include "contacts_page_styles.h"

namespace contacts::ui::style
{

// ---------- Colors (keep all visual tokens here) ----------
static constexpr uint32_t kGrayPanel = 0xFFF3DF;
static constexpr uint32_t kWhite = 0xFFF7E9;

static constexpr uint32_t kBtnBg = 0xFFF7E9;
static constexpr uint32_t kBtnBgSel = 0xEBA341;
static constexpr uint32_t kBtnBorder = 0xD9B06A;

static constexpr uint32_t kItemBg = 0xFFF7E9;
static constexpr uint32_t kItemBgFoc = 0xEBA341;
static constexpr uint32_t kItemBorder = 0xD9B06A;

static constexpr uint32_t kTextMain = 0x3A2A1A;
static constexpr uint32_t kTextMuted = 0x6A5646;

// ---------- Styles ----------
static bool s_inited = false;

static lv_style_t s_panel_side;
static lv_style_t s_panel_main;
static lv_style_t s_container_white;

static lv_style_t s_btn_basic;
static lv_style_t s_btn_filter_checked; // applied via LV_STATE_CHECKED

static lv_style_t s_item_base;
static lv_style_t s_item_focused; // applied via LV_STATE_FOCUSED

static lv_style_t s_label_primary;
static lv_style_t s_label_muted;

void init_once()
{
    if (s_inited) return;
    s_inited = true;

    // ---- Side panel (filter/action): gray background, no border, pad=3 ----
    lv_style_init(&s_panel_side);
    lv_style_set_bg_opa(&s_panel_side, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_side, lv_color_hex(kGrayPanel));
    lv_style_set_border_width(&s_panel_side, 0);
    lv_style_set_pad_all(&s_panel_side, 3);
    lv_style_set_radius(&s_panel_side, 0);

    // ---- Main panel (list): white background, no border, pad=3 ----
    lv_style_init(&s_panel_main);
    lv_style_set_bg_opa(&s_panel_main, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_main, lv_color_hex(kWhite));
    lv_style_set_border_width(&s_panel_main, 0);
    lv_style_set_pad_all(&s_panel_main, 3);
    lv_style_set_radius(&s_panel_main, 0);

    // ---- White containers (sub/bottom): white background, no border, pad=3 ----
    lv_style_init(&s_container_white);
    lv_style_set_bg_opa(&s_container_white, LV_OPA_COVER);
    lv_style_set_bg_color(&s_container_white, lv_color_hex(kWhite));
    lv_style_set_border_width(&s_container_white, 0);
    lv_style_set_pad_all(&s_container_white, 3);
    lv_style_set_radius(&s_container_white, 0);

    // ---- Buttons (common): gray bg, border=1, radius=12 ----
    lv_style_init(&s_btn_basic);
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_basic, lv_color_hex(kBtnBg));
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, lv_color_hex(kBtnBorder));
    lv_style_set_radius(&s_btn_basic, 12);
    lv_style_set_text_color(&s_btn_basic, lv_color_hex(kTextMain));

    // ---- Filter selected state: when CHECKED, use darker bg ----
    lv_style_init(&s_btn_filter_checked);
    lv_style_set_bg_opa(&s_btn_filter_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_filter_checked, lv_color_hex(kBtnBgSel));

    // ---- List item base ----
    lv_style_init(&s_item_base);
    lv_style_set_bg_opa(&s_item_base, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_base, lv_color_hex(kItemBg));
    lv_style_set_border_width(&s_item_base, 1);
    lv_style_set_border_color(&s_item_base, lv_color_hex(kItemBorder));
    lv_style_set_radius(&s_item_base, 6);

    // ---- List item focused state ----
    lv_style_init(&s_item_focused);
    lv_style_set_bg_opa(&s_item_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_focused, lv_color_hex(kItemBgFoc));
    lv_style_set_outline_width(&s_item_focused, 0);

    // ---- Labels ----
    lv_style_init(&s_label_primary);
    lv_style_set_text_color(&s_label_primary, lv_color_hex(kTextMain));

    lv_style_init(&s_label_muted);
    lv_style_set_text_color(&s_label_muted, lv_color_hex(kTextMuted));
}

// ---------------- Apply functions ----------------

void apply_panel_side(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_side, 0);
}

void apply_panel_main(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_main, 0);
}

void apply_container_white(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_container_white, 0);
}

void apply_btn_basic(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
}

void apply_btn_filter(lv_obj_t* btn)
{
    init_once();
    // base appearance
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
    // checked highlight
    lv_obj_add_style(btn, &s_btn_filter_checked, LV_PART_MAIN | LV_STATE_CHECKED);

    // Optional: make it behave like a checkable item when you toggle state in refresh_ui
    // (doesn't change behavior unless you set/clear LV_STATE_CHECKED)
    // lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
}

void apply_list_item(lv_obj_t* item)
{
    init_once();
    lv_obj_add_style(item, &s_item_base, LV_PART_MAIN);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
}

void apply_label_primary(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_primary, 0);
}

void apply_label_muted(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_muted, 0);
}

} // namespace contacts::ui::style
