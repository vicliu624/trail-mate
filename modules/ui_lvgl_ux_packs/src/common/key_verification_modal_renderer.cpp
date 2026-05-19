#include "ui_lvgl_ux_packs/common/key_verification_modal_renderer.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_lvgl_core/lvgl_focus_group.h"

#include <string>

namespace chat::ui
{

namespace
{

const char* modalTitle(
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot)
{
    if (snapshot.prompt ==
        ::ui::key_verification::VerificationPromptKind::ShowNumber)
    {
        return "Verification Number";
    }
    if (snapshot.prompt ==
        ::ui::key_verification::VerificationPromptKind::CompareCode)
    {
        return "Compare Verification Code";
    }
    return "Key Verification";
}

std::string modalDescription(
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot)
{
    std::string desc = snapshot.peer_label.c_str();
    if (!desc.empty())
    {
        desc += "\n";
    }
    desc += snapshot.status_line.c_str();
    if (!snapshot.verification_code.empty())
    {
        desc += "\n";
        desc += snapshot.verification_code.c_str();
    }
    if (snapshot.can_accept)
    {
        desc += "\n\n";
        desc += ::ui::i18n::tr("If it matches, trust the key.");
    }
    return desc;
}

lv_obj_t* createButton(lv_obj_t* parent,
                       const char* label_text,
                       lv_coord_t width,
                       lv_align_t align,
                       lv_coord_t x,
                       lv_coord_t y,
                       lv_event_cb_t callback,
                       void* user_data)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, width, ::ui::page_profile::resolve_control_button_height());
    lv_obj_align(button, align, x, y);

    lv_obj_t* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, label_text);
    lv_obj_center(label);

    if (callback != nullptr)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
        lv_obj_add_event_cb(button, callback, LV_EVENT_KEY, user_data);
    }
    return button;
}

} // namespace

void destroyKeyVerificationModal(
    KeyVerificationModalRefs& refs,
    std::unique_ptr<::ui::widgets::ImeWidget>& ime,
    bool restore_group)
{
    if (ime)
    {
        ime->detach();
        ime.reset();
    }

    if (refs.group && lv_group_get_default() == refs.group)
    {
        if (restore_group)
        {
            lv_group_t* restore_target = refs.prev_group ? refs.prev_group : app_g;
            set_default_group(restore_target);
        }
        else
        {
            set_default_group(nullptr);
        }
    }

    if (refs.overlay && lv_obj_is_valid(refs.overlay))
    {
        lv_obj_del(refs.overlay);
    }
    if (refs.group)
    {
        lv_group_del(refs.group);
    }

    refs = KeyVerificationModalRefs{};
}

void clearKeyVerificationError(const KeyVerificationModalRefs& refs)
{
    if (refs.error_label)
    {
        lv_label_set_text(refs.error_label, "");
    }
}

void renderKeyVerificationModal(
    lv_obj_t* parent,
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot,
    const KeyVerificationModalCallbacks& callbacks,
    KeyVerificationModalRefs& refs,
    std::unique_ptr<::ui::widgets::ImeWidget>& ime)
{
    destroyKeyVerificationModal(refs, ime, false);
    if (!parent || !snapshot.header.valid || snapshot.peer_id == 0)
    {
        return;
    }

    refs.prev_group = lv_group_get_default();
    refs.group = lv_group_create();
    set_default_group(refs.group);

    refs.overlay = lv_obj_create(parent);
    lv_obj_set_size(refs.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(refs.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(refs.overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(refs.overlay, 0, 0);
    lv_obj_set_style_pad_all(refs.overlay, 0, 0);
    lv_obj_set_style_radius(refs.overlay, 0, 0);
    lv_obj_clear_flag(refs.overlay, LV_OBJ_FLAG_SCROLLABLE);

    const auto& profile = ::ui::page_profile::current();
    const bool needs_number = snapshot.requires_number;
    const auto modal_size = ::ui::page_profile::resolve_modal_size(
        needs_number ? (profile.large_touch_hitbox ? 560 : 320) : 420,
        needs_number ? (profile.large_touch_hitbox ? 380 : 220) : 260,
        refs.overlay);

    refs.panel = lv_obj_create(refs.overlay);
    lv_obj_set_size(refs.panel, modal_size.width, modal_size.height);
    lv_obj_center(refs.panel);
    lv_obj_set_style_bg_color(refs.panel, lv_color_hex(0xFAF0D8), 0);
    lv_obj_set_style_bg_opa(refs.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(refs.panel, 1, 0);
    lv_obj_set_style_border_color(refs.panel, lv_color_hex(0xE7C98F), 0);
    lv_obj_set_style_radius(refs.panel, 10, 0);
    lv_obj_set_style_pad_all(refs.panel, ::ui::page_profile::resolve_modal_pad(), 0);
    lv_obj_clear_flag(refs.panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(refs.panel);
    ::ui::i18n::set_label_text(title, modalTitle(snapshot));
    lv_obj_set_style_text_color(title, lv_color_hex(0x6B4A1E), 0);
    lv_obj_set_style_text_font(
        title,
        ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()),
        0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    const std::string desc = modalDescription(snapshot);
    refs.desc = lv_label_create(refs.panel);
    ::ui::i18n::set_label_text_raw(refs.desc, desc.c_str());
    lv_obj_set_width(refs.desc, LV_PCT(100));
    lv_obj_set_style_text_align(refs.desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(refs.desc, lv_color_hex(0x8A6A3A), 0);
    lv_obj_align(refs.desc,
                 needs_number ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER,
                 0,
                 needs_number ? 34 : -8);

    if (needs_number)
    {
        refs.textarea = lv_textarea_create(refs.panel);
        lv_obj_set_width(refs.textarea, LV_PCT(100));
        lv_textarea_set_one_line(refs.textarea, true);
        lv_textarea_set_placeholder_text(refs.textarea, ::ui::i18n::tr("6 digits"));
        lv_textarea_set_accepted_chars(refs.textarea, "0123456789");
        lv_textarea_set_max_length(refs.textarea, 6);
        lv_obj_align(refs.textarea, LV_ALIGN_TOP_MID, 0, 72);

        refs.error_label = lv_label_create(refs.panel);
        ::ui::i18n::set_label_text_raw(refs.error_label, "");
        lv_obj_set_width(refs.error_label, LV_PCT(100));
        lv_obj_set_style_text_align(refs.error_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(refs.error_label, lv_color_hex(0xB94A2C), 0);
        lv_obj_align(refs.error_label, LV_ALIGN_TOP_MID, 0, 110);

        lv_obj_t* submit_btn = createButton(refs.panel,
                                            "Submit",
                                            LV_PCT(48),
                                            LV_ALIGN_BOTTOM_LEFT,
                                            0,
                                            0,
                                            callbacks.submit,
                                            callbacks.user_data);
        lv_obj_t* cancel_btn = createButton(refs.panel,
                                            "Cancel",
                                            LV_PCT(48),
                                            LV_ALIGN_BOTTOM_RIGHT,
                                            0,
                                            0,
                                            callbacks.close,
                                            callbacks.user_data);

        lv_group_add_obj(refs.group, refs.textarea);
        lv_group_add_obj(refs.group, submit_btn);
        lv_group_add_obj(refs.group, cancel_btn);
        lv_group_focus_obj(refs.textarea);

        if (::ui::page_profile::current().large_touch_hitbox)
        {
            ime.reset(new ::ui::widgets::ImeWidget());
            ime->init(refs.panel, refs.textarea);
            ime->setMode(::ui::widgets::ImeWidget::Mode::NUM);
        }
    }
    else
    {
        lv_obj_t* primary_btn = nullptr;
        if (snapshot.can_accept)
        {
            primary_btn = createButton(refs.panel,
                                       "Trust Key",
                                       LV_PCT(48),
                                       LV_ALIGN_BOTTOM_LEFT,
                                       0,
                                       0,
                                       callbacks.trust,
                                       callbacks.user_data);
            lv_group_add_obj(refs.group, primary_btn);
        }

        lv_obj_t* close_btn = createButton(
            refs.panel,
            snapshot.can_accept ? "Later" : "OK",
            snapshot.can_accept ? LV_PCT(48) : LV_PCT(100),
            snapshot.can_accept ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_MID,
            0,
            0,
            callbacks.close,
            callbacks.user_data);
        lv_group_add_obj(refs.group, close_btn);
        lv_group_focus_obj(primary_btn ? primary_btn : close_btn);
    }

    lv_obj_move_foreground(refs.overlay);
}

} // namespace chat::ui
