#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_message_list_styles.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_styles.h"

namespace chat::ui::message_list::styles
{
namespace
{

bool s_inited = false;
lv_style_t s_root;
lv_style_t s_label_font;

} // namespace

void init_once()
{
    ::ui::components::two_pane_styles::init_once();
    if (s_inited) return;
    s_inited = true;

    lv_style_init(&s_root);
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_bg_color(&s_root,
                          lv_color_hex(::ui::components::two_pane_styles::kSidePanelBg));
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);
    lv_style_set_radius(&s_root, 0);

    lv_style_init(&s_label_font);
    lv_style_set_text_font(&s_label_font, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()));
}

void apply_root_container(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_root, 0);
}

void apply_panel(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_main(obj);
}

void apply_filter_panel(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_side(obj);
}

void apply_item_btn(lv_obj_t* btn)
{
    if (::ui::components::info_card::use_tdeck_layout())
    {
        ::ui::components::info_card::apply_item_style(btn);
        return;
    }
    ::ui::components::two_pane_styles::apply_list_item(btn);
}

void apply_filter_btn(lv_obj_t* btn)
{
    ::ui::components::two_pane_styles::apply_btn_filter(btn);
}

void apply_filter_label(lv_obj_t* label)
{
    init_once();
    ::ui::components::two_pane_styles::apply_label_primary(label);
    ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), ::ui::fonts::ui_chrome_font());
}

void apply_label_name(lv_obj_t* label)
{
    init_once();
    ::ui::components::two_pane_styles::apply_label_primary(label);
    lv_obj_add_style(label, &s_label_font, 0);
}

void apply_label_preview(lv_obj_t* label)
{
    init_once();
    ::ui::components::two_pane_styles::apply_label_muted(label);
    lv_obj_add_style(label, &s_label_font, 0);
}

void apply_label_time(lv_obj_t* label)
{
    init_once();
    ::ui::components::two_pane_styles::apply_label_muted(label);
    lv_obj_add_style(label, &s_label_font, 0);
}

void apply_label_unread(lv_obj_t* label)
{
    init_once();
    ::ui::components::two_pane_styles::apply_label_accent(label);
    lv_obj_add_style(label, &s_label_font, 0);
}

void apply_label_placeholder(lv_obj_t* label)
{
    init_once();
    ::ui::components::two_pane_styles::apply_label_muted(label);
    lv_obj_add_style(label, &s_label_font, 0);
}

} // namespace chat::ui::message_list::styles

#endif
