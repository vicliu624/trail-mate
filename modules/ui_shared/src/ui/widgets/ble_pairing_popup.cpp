#include "ui/widgets/ble_pairing_popup.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/ui_theme.h"

namespace ui
{

lv_obj_t* BlePairingPopup::overlay_ = nullptr;
lv_obj_t* BlePairingPopup::panel_ = nullptr;
lv_obj_t* BlePairingPopup::title_label_ = nullptr;
lv_obj_t* BlePairingPopup::mode_label_ = nullptr;
lv_obj_t* BlePairingPopup::hint_label_ = nullptr;
lv_obj_t* BlePairingPopup::pin_label_ = nullptr;
lv_obj_t* BlePairingPopup::device_label_ = nullptr;
lv_obj_t* BlePairingPopup::footer_label_ = nullptr;
bool BlePairingPopup::visible_ = false;
uint32_t BlePairingPopup::shown_passkey_ = 0;
bool BlePairingPopup::shown_fixed_pin_ = false;

void BlePairingPopup::ensureCreated()
{
    if (overlay_)
    {
        return;
    }

    lv_obj_t* top = lv_layer_top();
    if (!top)
    {
        return;
    }

    overlay_ = lv_obj_create(top);
    lv_obj_remove_style_all(overlay_);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay_, ::ui::theme::color(::ui::theme::ColorSlot::OverlayScrim), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    panel_ = lv_obj_create(overlay_);
    lv_obj_set_style_bg_color(panel_, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 2, 0);
    lv_obj_set_style_border_color(panel_, ::ui::theme::border(), 0);
    lv_obj_set_style_radius(panel_, 16, 0);
    lv_obj_set_style_pad_left(panel_, 18, 0);
    lv_obj_set_style_pad_right(panel_, 18, 0);
    lv_obj_set_style_pad_top(panel_, 16, 0);
    lv_obj_set_style_pad_bottom(panel_, 14, 0);
    lv_obj_set_style_shadow_width(panel_, 18, 0);
    lv_obj_set_style_shadow_color(panel_, ::ui::theme::accent_strong(), 0);
    lv_obj_set_style_shadow_opa(panel_, LV_OPA_20, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel_, 8, 0);

    title_label_ = lv_label_create(panel_);
    ::ui::i18n::set_label_text(title_label_, "Bluetooth Pairing");
    lv_obj_set_style_text_color(title_label_, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title_label_, LV_PCT(100));

    mode_label_ = lv_label_create(panel_);
    lv_obj_set_style_bg_color(mode_label_, ::ui::theme::color(::ui::theme::ColorSlot::StateInfo), 0);
    lv_obj_set_style_bg_opa(mode_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(mode_label_, ::ui::theme::white(), 0);
    lv_obj_set_style_text_font(mode_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_pad_left(mode_label_, 10, 0);
    lv_obj_set_style_pad_right(mode_label_, 10, 0);
    lv_obj_set_style_pad_top(mode_label_, 4, 0);
    lv_obj_set_style_pad_bottom(mode_label_, 4, 0);
    lv_obj_set_style_radius(mode_label_, 10, 0);

    hint_label_ = lv_label_create(panel_);
    ::ui::i18n::set_label_text(hint_label_, "Enter this 6-digit PIN on your phone");
    lv_obj_set_style_text_color(hint_label_, ::ui::theme::text_muted(), 0);
    lv_obj_set_style_text_font(hint_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint_label_, LV_PCT(100));

    pin_label_ = lv_label_create(panel_);
    lv_obj_set_style_text_color(pin_label_, ::ui::theme::accent_strong(), 0);
    lv_obj_set_style_text_font(pin_label_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(pin_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(pin_label_, LV_PCT(100));

    device_label_ = lv_label_create(panel_);
    lv_obj_set_style_text_color(device_label_, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(device_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(device_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(device_label_, LV_PCT(100));

    footer_label_ = lv_label_create(panel_);
    ::ui::i18n::set_label_text(footer_label_, "Closes automatically after pairing");
    lv_obj_set_style_text_color(footer_label_, ::ui::theme::status_green(), 0);
    lv_obj_set_style_text_font(footer_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(footer_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(footer_label_, LV_PCT(100));

    applyLayout();
}

void BlePairingPopup::applyLayout()
{
    if (!overlay_ || !panel_)
    {
        return;
    }

    lv_obj_t* top = lv_layer_top();
    if (!top)
    {
        return;
    }

    const lv_coord_t screen_w = lv_obj_get_width(top);
    const lv_coord_t screen_h = lv_obj_get_height(top);
    lv_obj_set_size(overlay_, screen_w, screen_h);

    lv_coord_t panel_w = screen_w - 28;
    if (panel_w > 296)
    {
        panel_w = 296;
    }
    if (panel_w < 220)
    {
        panel_w = 220;
    }

    lv_coord_t panel_h = 198;
    if (screen_h < 220)
    {
        panel_h = screen_h - 20;
    }
    if (panel_h < 150)
    {
        panel_h = 150;
    }

    lv_obj_set_size(panel_, panel_w, panel_h);
    lv_obj_center(panel_);
}

void BlePairingPopup::show(uint32_t passkey, bool is_fixed_pin, const char* device_name)
{
    ensureCreated();
    if (!overlay_)
    {
        return;
    }

    applyLayout();

    char pin_buf[16];
    snprintf(pin_buf, sizeof(pin_buf), "%03lu %03lu",
             static_cast<unsigned long>(passkey / 1000U),
             static_cast<unsigned long>(passkey % 1000U));
    lv_label_set_text(pin_label_, pin_buf);
    ::ui::i18n::set_label_text(mode_label_, is_fixed_pin ? "Fixed PIN" : "Random PIN");
    lv_obj_set_style_bg_color(mode_label_,
                              is_fixed_pin ? ::ui::theme::status_green()
                                           : ::ui::theme::color(::ui::theme::ColorSlot::StateInfo),
                              0);

    std::string name_text = ::ui::i18n::format(
        "Device: %s",
        (device_name && device_name[0] != '\0') ? device_name : "Meshtastic");
    lv_label_set_text(device_label_, name_text.c_str());
    ::ui::fonts::apply_localized_font(device_label_, name_text.c_str(), &lv_font_montserrat_18);

    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_);
    visible_ = true;
    shown_passkey_ = passkey;
    shown_fixed_pin_ = is_fixed_pin;
}

void BlePairingPopup::update(uint32_t passkey, bool is_fixed_pin, const char* device_name)
{
    if (passkey == 0)
    {
        hide();
        return;
    }

    if (visible_ && shown_passkey_ == passkey && shown_fixed_pin_ == is_fixed_pin)
    {
        applyLayout();
        return;
    }

    show(passkey, is_fixed_pin, device_name);
}

void BlePairingPopup::hide()
{
    if (!overlay_)
    {
        visible_ = false;
        shown_passkey_ = 0;
        shown_fixed_pin_ = false;
        return;
    }

    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    visible_ = false;
    shown_passkey_ = 0;
    shown_fixed_pin_ = false;
}

bool BlePairingPopup::isVisible()
{
    return visible_;
}

} // namespace ui
