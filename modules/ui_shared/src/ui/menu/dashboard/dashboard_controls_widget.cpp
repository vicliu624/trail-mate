#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_key_focus.h"
#include "ui/menu/dashboard/dashboard_style.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui_presentation/controls/quick_controls.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace ui::menu::dashboard
{
namespace
{
struct ControlsWidget : WidgetLifetime
{
    DashboardKeyFocus focus{};
    DashboardCardChrome chrome{};
    ui::controls::IQuickControls* source = nullptr;
    ui::controls::Snapshot snapshot{};
    lv_obj_t *switches[3]{}, *names[3]{}, *status[3]{};
    lv_obj_t *sliders[2]{}, *values[2]{}, *message = nullptr, *hint = nullptr;
    bool applying = false;
};
lv_obj_t* caption(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y, lv_coord_t w, bool muted = false)
{
    auto* label = lv_label_create(parent);
    style_body_label(label, muted ? &lv_font_montserrat_12 : &lv_font_montserrat_14, muted ? color_text_dim() : color_text());
    ui::i18n::set_label_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}
void changed(lv_obj_t* label, const char* text)
{
    if (std::strcmp(lv_label_get_text(label), text)) ui::i18n::set_label_text_raw(label, text);
}
void refresh_controls(void* context)
{
    auto& self = *static_cast<ControlsWidget*>(context);
    if (self.source) self.source->snapshot(self.snapshot);
    const auto& data = self.snapshot;
    const ui::controls::SwitchState states[] = {data.wifi, data.lora, data.gps};
    for (unsigned i = 0; i < 3; i++)
    {
        const auto value = states[i];
        const bool enabled = self.source && value != ui::controls::SwitchState::Unavailable && value != ui::controls::SwitchState::Changing;
        const bool on = i == 0 ? data.wifi_enabled : i == 1 ? data.lora_enabled
                                                            : data.gps_enabled;
        const char* text = value == ui::controls::SwitchState::Changing ? "Switching..." : value == ui::controls::SwitchState::Failed    ? "Failed"
                                                                                       : value == ui::controls::SwitchState::Unavailable ? "Unavailable"
                                                                                       : on                                              ? "On"
                                                                                                                                         : "Off";
        changed(self.status[i], ui::i18n::tr(text));
        const auto background = on ? color_soft_amber() : color_panel_bg();
        if (lv_color_to_int(lv_obj_get_style_bg_color(self.switches[i], LV_PART_MAIN)) != lv_color_to_int(background))
            lv_obj_set_style_bg_color(self.switches[i], background, 0);
        if (enabled) lv_obj_remove_state(self.switches[i], LV_STATE_DISABLED);
        else lv_obj_add_state(self.switches[i], LV_STATE_DISABLED);
    }
    self.applying = true;
    for (unsigned i = 0; i < 2; i++)
    {
        const auto value = i ? data.brightness : data.volume;
        const bool enabled = self.source && (i ? data.brightness_supported : data.volume_supported);
        if (enabled) lv_obj_remove_state(self.sliders[i], LV_STATE_DISABLED);
        else lv_obj_add_state(self.sliders[i], LV_STATE_DISABLED);
        if (!lv_obj_has_state(self.sliders[i], LV_STATE_PRESSED)) lv_slider_set_value(self.sliders[i], value, LV_ANIM_OFF);
        char percent[16];
        std::snprintf(percent, sizeof(percent), enabled ? "%u%%" : "--", static_cast<unsigned>(lv_slider_get_value(self.sliders[i])));
        changed(self.values[i], percent);
    }
    self.applying = false;
    changed(self.message, data.message);
    if (data.message[0]) lv_obj_remove_flag(self.message, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(self.message, LV_OBJ_FLAG_HIDDEN);
    self.focus.ensure();
    if (self.hint)
        changed(self.hint, self.focus.editing ? "A/D adjust  Enter done  W/S wheel widgets  V menu" : "A/D select  Enter edit  W/S wheel widgets  V menu");
}
void switch_event(lv_event_t* event)
{
    auto& self = *static_cast<ControlsWidget*>(lv_event_get_user_data(event));
    if (!self.source) return;
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    const bool old = index == 0 ? self.snapshot.wifi_enabled : index == 1 ? self.snapshot.lora_enabled
                                                                          : self.snapshot.gps_enabled;
    if (!self.source->set(static_cast<ui::controls::Control>(index), old ? 0 : 1))
        std::snprintf(self.snapshot.message, sizeof(self.snapshot.message), "Change could not be applied");
    refresh_controls(&self);
}
void slider_event(lv_event_t* event)
{
    auto& self = *static_cast<ControlsWidget*>(lv_event_get_user_data(event));
    if (self.applying) return;
    auto* slider = lv_event_get_target_obj(event);
    const unsigned index = slider == self.sliders[1] ? 1 : 0;
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(lv_slider_get_value(slider)));
    ui::i18n::set_label_text_raw(self.values[index], percent);
    const auto* input = lv_indev_active();
    const bool keyboard = input && lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER;
    if (self.source && (lv_event_get_code(event) == LV_EVENT_RELEASED || keyboard))
        if (!self.source->set(index ? ui::controls::Control::Brightness : ui::controls::Control::Volume, lv_slider_get_value(slider)))
            ui::i18n::set_label_text(self.message, "Change could not be applied");
}
bool controls_key(void* context, uint32_t key)
{
    auto& self = *static_cast<ControlsWidget*>(context);
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    const bool left = key == 'a' || key == 'A' || key == LV_KEY_LEFT;
    const bool right = key == 'd' || key == 'D' || key == LV_KEY_RIGHT;
    if (left || right || key == LV_KEY_ENTER)
    {
        if (key == LV_KEY_ENTER)
        {
            if (self.focus.available(self.focus.selected))
            {
                if (self.focus.selected < 3) lv_obj_send_event(self.focus.current(), LV_EVENT_CLICKED, nullptr);
                else self.focus.editing = !self.focus.editing;
            }
        }
        else if (self.focus.editing && self.focus.selected >= 3)
        {
            const bool brightness = self.focus.selected == 4;
            if (self.source && self.focus.available(self.focus.selected))
                self.source->set(brightness ? ui::controls::Control::Brightness : ui::controls::Control::Volume,
                                 std::clamp<int>((brightness ? self.snapshot.brightness : self.snapshot.volume) + (left ? -5 : 5), brightness ? 10 : 0, 100));
            refresh_controls(&self);
        }
        else self.focus.move(left ? -1 : 1);
        changed(self.hint, self.focus.editing ? "A/D adjust  Enter done  W/S wheel widgets  V menu" : "A/D select  Enter edit  W/S wheel widgets  V menu");
        return true;
    }
#endif
    if (!self.source) return false;
    using namespace ui::controls;
    bool handled = true;
    if (key >= '1' && key <= '3')
    {
        const auto state = key == '1' ? self.snapshot.wifi : key == '2' ? self.snapshot.lora
                                                                        : self.snapshot.gps;
        if (state != SwitchState::Unavailable && state != SwitchState::Changing)
            self.source->set(static_cast<Control>(key - '1'), (key == '1' ? self.snapshot.wifi_enabled : key == '2' ? self.snapshot.lora_enabled
                                                                                                                    : self.snapshot.gps_enabled)
                                                                  ? 0
                                                                  : 1);
    }
    else if (key == '+' || key == '-' || key == '=') self.source->set(Control::Volume, std::clamp<int>(self.snapshot.volume + (key == '-' ? -5 : 5), 0, 100));
    else if (key == '[' || key == ']') self.source->set(Control::Brightness, std::clamp<int>(self.snapshot.brightness + (key == '[' ? -5 : 5), 10, 100));
    else handled = false;
    if (handled) refresh_controls(&self);
    return handled;
}
} // namespace
WidgetHandle create_controls_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ui::controls::IQuickControls* source)
{
    auto* self = create_widget_state<ControlsWidget>();
    if (!self) return {};
    self->source = source;
    self->chrome = create_card_chrome(parent, ui::i18n::tr("Quick controls"), width, height);
    auto* body = self->chrome.body;
    // Touch devices need no footer. Pager uses a compact, transparent key hint.
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    lv_obj_set_height(self->chrome.footer, 18);
    lv_obj_set_style_bg_opa(self->chrome.footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(self->chrome.footer, 0, 0);
    lv_obj_set_style_pad_bottom(self->chrome.footer, 0, 0);
    self->hint = caption(self->chrome.footer, "A/D select  Enter edit  W/S wheel widgets  V menu", 0, 0, LV_PCT(100), true);
#else
    lv_obj_delete(self->chrome.footer);
    self->chrome.footer = nullptr;
#endif
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_height(body, lv_obj_get_content_height(self->chrome.card) - lv_obj_get_y(body) - (self->hint ? 18 : 0));
    lv_obj_update_layout(body);
    const auto w = lv_obj_get_content_width(body), h = lv_obj_get_content_height(body);
    const bool tall = h > 180;
    const lv_coord_t content_w = std::min<lv_coord_t>(w, tall ? 560 : 560), x = (w - content_w) / 2;
    const lv_coord_t gap = tall ? 16 : 8, toggle_w = (content_w - 2 * gap) / 3, toggle_h = tall ? 76 : 44;
    const lv_coord_t top = tall ? std::max<lv_coord_t>(8, (h - 278) / 2) : std::max<lv_coord_t>(0, (h - 96) / 2);
    const char* names[] = {"Wi-Fi", "LoRa", "GPS"};
    for (unsigned i = 0; i < 3; i++)
    {
        auto* button = lv_button_create(body);
        self->switches[i] = button;
        self->focus.add(button);
        lv_group_remove_obj(button);
        lv_obj_set_pos(button, x + i * (toggle_w + gap), top);
        lv_obj_set_size(button, toggle_w, toggle_h);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_radius(button, 10, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, color_line(), 0);
        self->names[i] = caption(button, names[i], 4, tall ? 16 : 3, toggle_w - 8);
        self->status[i] = caption(button, "--", 4, tall ? 42 : 22, toggle_w - 8, true);
        lv_obj_set_style_text_align(self->names[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(self->status[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(button, switch_event, LV_EVENT_CLICKED, self);
    }
    const lv_coord_t slider_w = tall ? content_w : (content_w - gap) / 2;
    for (unsigned i = 0; i < 2; i++)
    {
        const lv_coord_t sx = tall ? x : x + i * (slider_w + gap), sy = top + toggle_h + (tall ? 38 + static_cast<lv_coord_t>(i) * 82 : 8);
        caption(body, i ? "Brightness" : "Volume", sx, sy, slider_w - 42);
        self->values[i] = caption(body, "--", sx + slider_w - 40, sy, 40, true);
        lv_obj_set_style_text_align(self->values[i], LV_TEXT_ALIGN_RIGHT, 0);
        auto* slider = lv_slider_create(body);
        self->sliders[i] = slider;
        self->focus.add(slider);
        lv_group_remove_obj(slider);
        lv_obj_set_pos(slider, sx + 7, sy + (tall ? 34 : 26));
        lv_obj_set_size(slider, slider_w - 14, tall ? 8 : 6);
        lv_slider_set_range(slider, i ? 10 : 0, 100);
        lv_obj_set_style_bg_color(slider, color_line(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, color_amber(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, color_amber_dark(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
        lv_obj_set_ext_click_area(slider, 10);
        lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED, self);
        lv_obj_add_event_cb(slider, slider_event, LV_EVENT_RELEASED, self);
    }
    self->message = caption(body, "", 0, h - 16, LV_PCT(100), true);
    lv_obj_add_flag(self->message, LV_OBJ_FLAG_HIDDEN);
    set_status_chip(self->chrome, "Device", color_soft_amber(), color_text());
    auto handle = own_widget(self->chrome.card, self, refresh_controls);
    handle.key = controls_key;
    refresh_controls(self);
    return handle;
}
} // namespace ui::menu::dashboard
