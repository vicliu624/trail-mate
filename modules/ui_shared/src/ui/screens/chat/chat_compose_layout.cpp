#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_compose_layout.h
 * @brief Chat compose screen layout (structure only)
 *
 * Wireframe (structure only):
 *
 *  +----------------------------------------------+
 *  | TopBar: [Title]                      [RSSI] |
 *  +----------------------------------------------+
 *  | Content (grow)                               |
 *  |  +----------------------------------------+  |
 *  |  | TextArea (multi-line, grow)           |  |
 *  |  +----------------------------------------+  |
 *  +----------------------------------------------+
 *  | ActionBar: [Send] [Position] [Cancel] Len:x |
 *  +----------------------------------------------+
 *
 * Tree view:
 * - container(root, column)
 * - top_bar (widget host on container)
 * - content (column, grow=1, not scrollable)
 *   - textarea (grow=1)
 * - action_bar (row)
 *   - send_btn
 *   - send_label
 *   - position_btn
 *   - position_label
 *   - cancel_btn
 *   - cancel_label
 *   - len_label
 */
#include "ui/screens/chat/chat_compose_layout.h"

#include <algorithm>

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/presentation/chat_compose_scaffold.h"
#include "ui/ui_theme.h"

namespace chat::ui::compose::layout
{
namespace compose_layout = ::ui::presentation::chat_compose_scaffold;

static lv_obj_t* create_btn_with_label(lv_obj_t* parent,
                                       int w,
                                       int h,
                                       const char* text,
                                       compose_layout::ActionButtonRole role)
{
    compose_layout::ActionButtonSpec button_spec{};
    button_spec.width = w;
    button_spec.height = h;
    lv_obj_t* label = nullptr;
    lv_obj_t* btn = compose_layout::create_action_button(parent, label, role, button_spec);
    ::ui::theme::ComponentProfile profile{};
    (void)::ui::theme::resolve_component_profile(
        role == compose_layout::ActionButtonRole::Primary
            ? ::ui::theme::ComponentSlot::ActionButtonPrimary
            : ::ui::theme::ComponentSlot::ActionButtonSecondary,
        profile);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_color(label,
                                profile.text_color.present ? profile.text_color.value
                                                           : ::ui::theme::text(),
                                0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return btn;
}

void create(lv_obj_t* parent, const Spec& spec, Widgets& w)
{
    const auto& profile = ::ui::page_profile::current();
    const int button_height = std::max(spec.btn_h, static_cast<int>(profile.filter_button_height));
    const int action_bar_height = std::max(spec.action_bar_h, button_height + 16);
    const int send_width = std::max(spec.send_w, profile.large_touch_hitbox ? 108 : spec.send_w);
    const int position_width = std::max(spec.position_w, profile.large_touch_hitbox ? 124 : spec.position_w);
    const int cancel_width = std::max(spec.cancel_w, profile.large_touch_hitbox ? 108 : spec.cancel_w);
    const int content_pad = std::max(spec.content_pad, static_cast<int>(profile.content_pad_left));
    const int content_row_pad = std::max(spec.content_row_pad, profile.large_touch_hitbox ? 8 : spec.content_row_pad);
    const int action_pad_lr = std::max(spec.action_pad_lr, profile.large_touch_hitbox ? 14 : spec.action_pad_lr);
    const int action_pad_tb = std::max(spec.action_pad_tb, profile.large_touch_hitbox ? 6 : spec.action_pad_tb);
    const int button_gap = std::max(spec.btn_gap, profile.large_touch_hitbox ? 12 : spec.btn_gap);

    compose_layout::RootSpec root_spec{};
    compose_layout::HeaderSpec header_spec{};
    compose_layout::ContentSpec content_spec{};
    compose_layout::EditorSpec editor_spec{};
    compose_layout::ActionBarSpec action_bar_spec{};

    w.container = compose_layout::create_root(parent, root_spec);
    lv_obj_t* header = compose_layout::create_header_container(w.container, header_spec);

    ::ui::widgets::TopBarConfig top_bar_cfg{};
    top_bar_cfg.height = profile.top_bar_height;
    ::ui::widgets::top_bar_init(w.top_bar, header, top_bar_cfg);

    content_spec.pad_all = content_pad;
    content_spec.pad_row = content_row_pad;
    w.content = compose_layout::create_content_region(w.container, content_spec);

    editor_spec.grow = true;
    w.textarea = compose_layout::create_editor(w.content, editor_spec);

    action_bar_spec.height = action_bar_height;
    action_bar_spec.pad_left = action_pad_lr;
    action_bar_spec.pad_right = action_pad_lr;
    action_bar_spec.pad_top = action_pad_tb;
    action_bar_spec.pad_bottom = action_pad_tb;
    action_bar_spec.pad_column = button_gap;
    w.action_bar = compose_layout::create_action_bar(w.container, action_bar_spec);

    w.send_btn = create_btn_with_label(w.action_bar,
                                       send_width,
                                       button_height,
                                       "Send",
                                       compose_layout::ActionButtonRole::Primary);

    w.position_btn = create_btn_with_label(w.action_bar,
                                           position_width,
                                           button_height,
                                           "Position",
                                           compose_layout::ActionButtonRole::Secondary);

    w.cancel_btn = create_btn_with_label(w.action_bar,
                                         cancel_width,
                                         button_height,
                                         "Cancel",
                                         compose_layout::ActionButtonRole::Secondary);

    lv_obj_t* spacer = compose_layout::create_flex_spacer(w.action_bar);

    w.len_label = lv_label_create(w.action_bar);
    ::ui::i18n::set_label_text_fmt(w.len_label, "Remain: %u", 233U);
    lv_obj_set_style_text_color(w.len_label, ::ui::theme::text_muted(), 0);
}

} // namespace chat::ui::compose::layout

#endif
