#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_message_list_layout.cpp
 * @brief Layout (structure only) for ChatMessageListScreen
 */

#include "ui/screens/chat/chat_message_list_layout.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_layout.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"

#include "sys/clock.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

namespace chat::ui::layout
{
namespace
{

constexpr int kPanelGap = 0;
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kSecondsPerMonth = 30U * kSecondsPerDay;
constexpr uint32_t kSecondsPerYear = 365U * kSecondsPerDay;

struct Metrics
{
    int filter_panel_width = 80;
    int filter_button_height = 28;
    int list_item_height = 28;
    int name_x = 10;
    int preview_x = 120;
    int preview_width = 130;
    int unread_x = 42;
    int time_x = 10;
};

Metrics current_metrics()
{
    const auto& profile = ::ui::page_profile::current();
    Metrics metrics{};
    metrics.filter_panel_width = profile.filter_panel_width;
    metrics.filter_button_height = profile.filter_button_height;
    metrics.list_item_height = profile.list_item_height;
    if (profile.large_touch_hitbox)
    {
        metrics.name_x = 16;
        metrics.preview_x = 170;
        metrics.preview_width = 280;
        metrics.unread_x = 72;
        metrics.time_x = 16;
    }
    else
    {
        metrics.filter_panel_width = std::max(metrics.filter_panel_width, 104);
    }
    return metrics;
}

const char* broadcast_filter_text()
{
    return ::ui::page_profile::current().large_touch_hitbox ? "Broadcast" : "Bcast";
}

bool is_valid_epoch_ts(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}

void format_time_hhmm(char out[16], uint32_t ts)
{
    if (ts == 0)
    {
        std::snprintf(out, 16, "--:--");
        return;
    }
    if (!is_valid_epoch_ts(ts))
    {
        uint32_t now_epoch = sys::epoch_seconds_now();
        uint32_t now_secs = is_valid_epoch_ts(now_epoch)
                                ? now_epoch
                                : static_cast<uint32_t>(sys::millis_now() / 1000U);
        if (now_secs < ts)
        {
            now_secs = ts;
        }
        uint32_t diff = now_secs - ts;
        if (diff < 60U)
        {
            std::snprintf(out, 16, "%s", ::ui::i18n::tr("now"));
        }
        else if (diff < 3600U)
        {
            std::snprintf(out, 16, "%s", ::ui::i18n::format("%um", static_cast<unsigned>(diff / 60U)).c_str());
        }
        else if (diff < kSecondsPerDay)
        {
            std::snprintf(out, 16, "%s", ::ui::i18n::format("%uh", static_cast<unsigned>(diff / 3600U)).c_str());
        }
        else if (diff < kSecondsPerMonth)
        {
            std::snprintf(out, 16, "%s", ::ui::i18n::format("%ud", static_cast<unsigned>(diff / kSecondsPerDay)).c_str());
        }
        else if (diff < kSecondsPerYear)
        {
            std::snprintf(out, 16, "%s", ::ui::i18n::format("%umo", static_cast<unsigned>(diff / kSecondsPerMonth)).c_str());
        }
        else
        {
            std::snprintf(out, 16, "%s", ::ui::i18n::format("%uy", static_cast<unsigned>(diff / kSecondsPerYear)).c_str());
        }
        return;
    }

    time_t t = ui_apply_timezone_offset(static_cast<time_t>(ts));
    struct tm* info = gmtime(&t);
    if (info)
    {
        strftime(out, 16, "%H:%M", info);
    }
    else
    {
        std::snprintf(out, 16, "--:--");
    }
}

std::string truncate_preview(const std::string& text)
{
    static constexpr size_t kMaxPreviewBytes = 18;
    if (text.size() <= kMaxPreviewBytes)
    {
        return text;
    }

    std::string out = text.substr(0, kMaxPreviewBytes);
    out.append("...");
    return out;
}

void replace_all(std::string& text, const char* from, const char* to)
{
    if (!from || !to || !*from)
    {
        return;
    }

    std::string::size_type pos = 0;
    const std::string from_text(from);
    const std::string to_text(to);
    while ((pos = text.find(from_text, pos)) != std::string::npos)
    {
        text.replace(pos, from_text.size(), to_text);
        pos += to_text.size();
    }
}

std::string compact_list_name(const std::string& name)
{
    if (!::ui::components::info_card::use_tdeck_layout())
    {
        return name;
    }

    std::string compact = name;
    replace_all(compact, ::ui::i18n::tr("Broadcast"), ::ui::i18n::tr("Bcast"));
    replace_all(compact, "Primary", "Pri");
    replace_all(compact, "Secondary", "Sec");
    return compact;
}

std::string build_list_title(const chat::ConversationMeta& conv)
{
    return "[" + std::string(chat::infra::meshProtocolShortName(conv.id.protocol)) +
           "] " + compact_list_name(conv.name);
}

void style_filter_label(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }

    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), ::ui::fonts::ui_chrome_font());
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

lv_obj_t* create_root(lv_obj_t* parent)
{
    return ::ui::components::two_pane_layout::create_root(parent);
}

lv_obj_t* create_content(lv_obj_t* parent)
{
    return ::ui::components::two_pane_layout::create_content_row(parent);
}

lv_obj_t* create_filter_panel(lv_obj_t* parent,
                              lv_obj_t** direct_btn,
                              lv_obj_t** broadcast_btn,
                              lv_obj_t** team_btn)
{
    const auto& profile = ::ui::page_profile::current();
    const Metrics metrics = current_metrics();

    ::ui::components::two_pane_layout::SidePanelSpec panel_spec;
    panel_spec.width = metrics.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = profile.large_touch_hitbox ? 8 : kPanelGap;
    lv_obj_t* panel = ::ui::components::two_pane_layout::create_side_panel(parent, panel_spec);

    lv_obj_t* direct = lv_btn_create(panel);
    lv_obj_set_size(direct, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(direct);
    lv_obj_t* direct_label = lv_label_create(direct);
    ::ui::i18n::set_label_text(direct_label, "Direct");
    style_filter_label(direct_label);
    lv_obj_center(direct_label);

    lv_obj_t* broadcast = lv_btn_create(panel);
    lv_obj_set_size(broadcast, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(broadcast);
    lv_obj_t* broadcast_label = lv_label_create(broadcast);
    ::ui::i18n::set_label_text(broadcast_label, broadcast_filter_text());
    style_filter_label(broadcast_label);
    lv_obj_center(broadcast_label);

    lv_obj_t* team = lv_btn_create(panel);
    lv_obj_set_size(team, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(team);
    lv_obj_t* team_label = lv_label_create(team);
    ::ui::i18n::set_label_text(team_label, "Team");
    style_filter_label(team_label);
    lv_obj_center(team_label);
    lv_obj_add_flag(team, LV_OBJ_FLAG_HIDDEN);

    if (direct_btn) *direct_btn = direct;
    if (broadcast_btn) *broadcast_btn = broadcast;
    if (team_btn) *team_btn = team;
    return panel;
}

lv_obj_t* create_list_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();

    ::ui::components::two_pane_layout::MainPanelSpec panel_spec;
    panel_spec.pad_all = profile.large_touch_hitbox ? 6 : 3;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.pad_left = profile.list_panel_pad_left;
    panel_spec.pad_right = profile.list_panel_pad_right;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    return ::ui::components::two_pane_layout::create_main_panel(parent, panel_spec);
}

} // namespace

MessageListLayout create_layout(lv_obj_t* parent)
{
    MessageListLayout w{};
    w.root = create_root(parent);
    w.content = create_content(w.root);
    w.filter_panel = create_filter_panel(w.content, &w.direct_btn, &w.broadcast_btn, &w.team_btn);
    w.list_panel = create_list_panel(w.content);
    return w;
}

MessageItemWidgets create_message_item(lv_obj_t* parent)
{
    const Metrics metrics = current_metrics();

    MessageItemWidgets w{};
    w.btn = lv_obj_create(parent);
    lv_obj_add_flag(w.btn, LV_OBJ_FLAG_CLICKABLE);
    if (::ui::components::info_card::use_tdeck_layout())
    {
        ::ui::components::info_card::ContentOptions options{};
        options.header_meta = true;
        options.body_meta = true;
        ::ui::components::info_card::configure_item(w.btn, metrics.list_item_height);
        const auto slots = ::ui::components::info_card::create_content(w.btn, options);
        w.name_label = slots.header_main_label;
        w.time_label = slots.header_meta_label;
        w.preview_label = slots.body_main_label;
        w.unread_label = slots.body_meta_label;
    }
    else
    {
        lv_obj_set_size(w.btn, LV_PCT(100), metrics.list_item_height);
        ::ui::components::two_pane_layout::make_non_scrollable(w.btn);

        w.name_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.name_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_align(w.name_label, LV_ALIGN_LEFT_MID, metrics.name_x, 0);

        w.preview_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.preview_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_align(w.preview_label, LV_ALIGN_LEFT_MID, metrics.preview_x, 0);
        lv_label_set_long_mode(w.preview_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(w.preview_label, metrics.preview_width);

        w.time_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.time_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_align(w.time_label, LV_ALIGN_RIGHT_MID, -metrics.time_x, 0);

        w.unread_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.unread_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_align(w.unread_label, LV_ALIGN_RIGHT_MID, -metrics.unread_x, 0);
    }

    return w;
}

void populate_message_item(const MessageItemWidgets& widgets,
                           const chat::ConversationMeta& conv)
{
    const bool use_info_card = ::ui::components::info_card::use_tdeck_layout();

    const std::string title = build_list_title(conv);
    lv_label_set_text(widgets.name_label, title.c_str());
    ::ui::fonts::apply_chat_content_font(widgets.name_label, title.c_str());
    if (use_info_card)
    {
        ::ui::components::info_card::apply_single_line_label(widgets.name_label);
    }

    const std::string preview = truncate_preview(conv.preview);
    lv_label_set_text(widgets.preview_label, preview.c_str());
    ::ui::fonts::apply_chat_content_font(widgets.preview_label, preview.c_str());

    char time_buf[16];
    format_time_hhmm(time_buf, conv.last_timestamp);
    lv_label_set_text(widgets.time_label, time_buf);
    ::ui::fonts::apply_localized_font(widgets.time_label, time_buf, ::ui::fonts::ui_chrome_font());
    if (use_info_card)
    {
        ::ui::components::info_card::apply_single_line_label(widgets.time_label);
    }

    if (conv.unread > 0)
    {
        char unread_str[16];
        std::snprintf(unread_str, sizeof(unread_str), "%d", conv.unread);
        lv_label_set_text(widgets.unread_label, unread_str);
    }
    else
    {
        lv_label_set_text(widgets.unread_label, "");
    }
    ::ui::fonts::apply_localized_font(
        widgets.unread_label, lv_label_get_text(widgets.unread_label), ::ui::fonts::ui_chrome_font());
    if (use_info_card)
    {
        ::ui::components::info_card::apply_single_line_label(widgets.unread_label);
    }
}

lv_obj_t* create_placeholder(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return label;
}

} // namespace chat::ui::layout

#endif
