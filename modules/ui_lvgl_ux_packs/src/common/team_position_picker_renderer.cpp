#include "ui_lvgl_ux_packs/common/team_position_picker_renderer.h"

#include "team/protocol/team_location_marker.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui_lvgl_core/lvgl_focus_group.h"

extern "C"
{
    extern const lv_image_dsc_t AreaCleared;
    extern const lv_image_dsc_t BaseCamp;
    extern const lv_image_dsc_t GoodFind;
    extern const lv_image_dsc_t rally;
    extern const lv_image_dsc_t sos;
}

namespace chat
{
namespace ui
{

namespace
{

struct TeamPositionIconOption
{
    uint8_t icon_id;
    const char* meaning;
    const lv_image_dsc_t* image;
};

const TeamPositionIconOption kTeamPositionIconOptions[] = {
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::AreaCleared),
     "Area Cleared", &AreaCleared},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::BaseCamp),
     "Base Camp", &BaseCamp},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::GoodFind),
     "Good Find", &GoodFind},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally),
     "Rally Point", &rally},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Sos),
     "Emergency SOS", &sos}};

const TeamPositionIconOption* findTeamPositionIconOption(uint8_t icon_id)
{
    for (const auto& item : kTeamPositionIconOptions)
    {
        if (item.icon_id == icon_id)
        {
            return &item;
        }
    }
    return nullptr;
}

lv_style_selector_t focusedSelector()
{
    return static_cast<lv_style_selector_t>(
        static_cast<unsigned>(LV_PART_MAIN) |
        static_cast<unsigned>(LV_STATE_FOCUSED));
}

} // namespace

TeamPositionPickerRenderer::TeamPositionPickerRenderer(
    lv_obj_t* parent,
    const Callbacks& callbacks)
    : parent_(parent), callbacks_(callbacks)
{
}

TeamPositionPickerRenderer::~TeamPositionPickerRenderer()
{
    close(false);
}

bool TeamPositionPickerRenderer::isOpen() const
{
    return overlay_ != nullptr;
}

void TeamPositionPickerRenderer::updateHint(uint8_t icon_id)
{
    if (!desc_)
    {
        return;
    }
    if (icon_id == 0)
    {
        ::ui::i18n::set_label_text(desc_, "Cancel");
        return;
    }

    const TeamPositionIconOption* option = findTeamPositionIconOption(icon_id);
    if (option)
    {
        ::ui::i18n::set_label_text(desc_, option->meaning);
        return;
    }
    ::ui::i18n::set_label_text(desc_, "Select marker");
}

void TeamPositionPickerRenderer::handleIconFocused(uint8_t icon_id,
                                                   lv_obj_t* target)
{
    updateHint(icon_id);
    if (target)
    {
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
    }
}

void TeamPositionPickerRenderer::handleIconSelected(uint8_t icon_id)
{
    if (callbacks_.on_icon_selected)
    {
        callbacks_.on_icon_selected(callbacks_.user_data, icon_id);
    }
}

void TeamPositionPickerRenderer::handleCancelFocused()
{
    updateHint(0);
}

void TeamPositionPickerRenderer::handleCancel()
{
    if (callbacks_.on_cancel)
    {
        callbacks_.on_cancel(callbacks_.user_data);
    }
}

void TeamPositionPickerRenderer::iconEventCb(lv_event_t* e)
{
    auto* ctx = static_cast<IconEventCtx*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->renderer)
    {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        ctx->renderer->handleIconFocused(ctx->icon_id,
                                         lv_event_get_target_obj(e));
        return;
    }
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        ctx->renderer->handleIconSelected(ctx->icon_id);
    }
}

void TeamPositionPickerRenderer::cancelEventCb(lv_event_t* e)
{
    auto* renderer =
        static_cast<TeamPositionPickerRenderer*>(lv_event_get_user_data(e));
    if (!renderer)
    {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        renderer->handleCancelFocused();
        return;
    }
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        renderer->handleCancel();
    }
}

bool TeamPositionPickerRenderer::open()
{
    if (!parent_ || isOpen())
    {
        return false;
    }

    prev_group_ = lv_group_get_default();
    group_ = lv_group_create();
    set_default_group(group_);

    overlay_ = lv_obj_create(parent_);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    const auto& profile = ::ui::page_profile::current();
    const auto modal_size = ::ui::page_profile::resolve_modal_size(
        profile.large_touch_hitbox ? 560 : 320,
        profile.large_touch_hitbox ? 320 : 186,
        overlay_);
    const lv_coord_t icon_button_size =
        ::ui::page_profile::resolve_icon_picker_button_size();
    const lv_coord_t title_bar_height =
        ::ui::page_profile::resolve_popup_title_height();

    panel_ = lv_obj_create(overlay_);
    lv_obj_set_size(panel_, modal_size.width, modal_size.height);
    lv_obj_center(panel_);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(0xFAF0D8), 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);
    lv_obj_set_style_border_color(panel_, lv_color_hex(0xE7C98F), 0);
    lv_obj_set_style_radius(panel_, 10, 0);
    lv_obj_set_style_pad_all(
        panel_,
        ::ui::page_profile::resolve_modal_pad(),
        0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(panel_);
    lv_obj_set_size(title_bar, LV_PCT(100), title_bar_height);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0xEBA341), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 6, 0);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, -4);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(title_bar);
    ::ui::i18n::set_label_text(title, "Share Position Marker");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6B4A1E), 0);
    lv_obj_center(title);

    lv_obj_t* icon_row = lv_obj_create(panel_);
    lv_obj_set_size(icon_row,
                    LV_PCT(100),
                    profile.large_touch_hitbox ? (icon_button_size + 20) : 72);
    lv_obj_set_style_bg_opa(icon_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_row, 0, 0);
    lv_obj_set_style_pad_all(icon_row, 0, 0);
    lv_obj_set_style_pad_column(icon_row,
                                profile.large_touch_hitbox ? 12 : 6,
                                0);
    lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(icon_row,
                 LV_ALIGN_TOP_MID,
                 0,
                 profile.large_touch_hitbox ? (title_bar_height + 8) : 24);
    lv_obj_set_scroll_dir(icon_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(icon_row, LV_SCROLLBAR_MODE_OFF);

    clearIconContexts();
    for (const auto& option : kTeamPositionIconOptions)
    {
        if (icon_ctx_count_ >= kMaxIcons)
        {
            break;
        }

        lv_obj_t* btn = lv_btn_create(icon_row);
        lv_obj_set_size(btn, icon_button_size, icon_button_size);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xF6E6C6), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xE7C98F), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        const lv_style_selector_t focused_selector = focusedSelector();
        lv_obj_set_style_outline_width(btn, 2, focused_selector);
        lv_obj_set_style_outline_color(btn,
                                       lv_color_hex(0xC98118),
                                       focused_selector);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* img = lv_image_create(btn);
        lv_image_set_src(img, option.image);
        lv_obj_center(img);

        IconEventCtx& ctx = icon_ctxs_[icon_ctx_count_++];
        ctx.renderer = this;
        ctx.icon_id = option.icon_id;

        lv_obj_add_event_cb(btn, iconEventCb, LV_EVENT_CLICKED, &ctx);
        lv_obj_add_event_cb(btn, iconEventCb, LV_EVENT_KEY, &ctx);
        lv_obj_add_event_cb(btn, iconEventCb, LV_EVENT_FOCUSED, &ctx);

        lv_group_add_obj(group_, btn);
    }

    desc_ = lv_label_create(panel_);
    ::ui::i18n::set_label_text(desc_, "Select marker");
    lv_obj_set_width(desc_, LV_PCT(100));
    lv_obj_set_style_text_align(desc_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(desc_, lv_color_hex(0x8A6A3A), 0);
    lv_obj_align(desc_,
                 LV_ALIGN_TOP_MID,
                 0,
                 profile.large_touch_hitbox
                     ? (title_bar_height + icon_button_size + 28)
                     : 104);

    lv_obj_t* cancel_btn = lv_btn_create(panel_);
    lv_obj_set_size(cancel_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xF6E6C6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel_btn,
                                  lv_color_hex(0xE7C98F),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 6, LV_PART_MAIN);
    const lv_style_selector_t focused_selector = focusedSelector();
    lv_obj_set_style_outline_width(cancel_btn, 2, focused_selector);
    lv_obj_set_style_outline_color(cancel_btn,
                                   lv_color_hex(0xC98118),
                                   focused_selector);
    lv_obj_align(cancel_btn,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 profile.large_touch_hitbox ? -12 : -6);
    lv_obj_clear_flag(cancel_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0x6B4A1E), 0);
    lv_obj_center(cancel_label);

    lv_obj_add_event_cb(cancel_btn, cancelEventCb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(cancel_btn, cancelEventCb, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(cancel_btn, cancelEventCb, LV_EVENT_FOCUSED, this);
    lv_group_add_obj(group_, cancel_btn);

    if (icon_ctx_count_ > 0 && group_)
    {
        lv_obj_t* first_btn = lv_group_get_focused(group_);
        if (!first_btn)
        {
            first_btn = lv_obj_get_child(icon_row, 0);
        }
        if (first_btn)
        {
            lv_group_focus_obj(first_btn);
        }
        updateHint(icon_ctxs_[0].icon_id);
    }
    lv_obj_move_foreground(overlay_);
    return true;
}

void TeamPositionPickerRenderer::close(bool restore_group)
{
    clearIconContexts();

    if (group_ && lv_group_get_default() == group_)
    {
        if (restore_group)
        {
            lv_group_t* restore_target = prev_group_ ? prev_group_ : app_g;
            set_default_group(restore_target);
        }
        else
        {
            set_default_group(nullptr);
        }
    }

    if (overlay_ && lv_obj_is_valid(overlay_))
    {
        lv_obj_del(overlay_);
    }

    if (group_)
    {
        lv_group_del(group_);
    }

    overlay_ = nullptr;
    panel_ = nullptr;
    desc_ = nullptr;
    group_ = nullptr;
    prev_group_ = nullptr;
}

void TeamPositionPickerRenderer::clearIconContexts()
{
    for (std::size_t i = 0; i < icon_ctx_count_; ++i)
    {
        icon_ctxs_[i] = IconEventCtx{};
    }
    icon_ctx_count_ = 0;
}

} // namespace ui
} // namespace chat
