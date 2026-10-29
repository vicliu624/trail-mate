#include "ui/menu/dashboard/dashboard_widgets.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_style.h"

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

namespace ui::menu::dashboard
{
namespace
{

struct RecentWidgetUi : WidgetLifetime
{
    DashboardCardChrome chrome{};
    lv_obj_t* rows[2]{};
    lv_obj_t* row_dots[2]{};
    lv_obj_t* row_names[2]{};
    lv_obj_t* row_previews[2]{};
    std::array<char, 256> row_name_text[2]{};
    std::array<char, 1024> row_preview_text[2]{};
    lv_obj_t* row_badges[2]{};
    lv_obj_t* row_badge_labels[2]{};
    lv_obj_t* scan_track = nullptr;
    lv_obj_t* scan_runner = nullptr;
    lv_obj_t* footer_label = nullptr;
};

constexpr size_t kVisibleRows = 2;

template <size_t N>
void set_recent_text(lv_obj_t* label, std::array<char, N>& cached, const char* text)
{
    static_assert(N >= 4, "Recent text needs room for an ellipsis and terminator");
    if (!label || !text) return;
    size_t length = 0;
    while (length < N && text[length]) ++length;
    const bool truncated = length == N;
    size_t prefix = truncated ? N - 4 : length;
    // The first excluded byte must begin a UTF-8 character. Do not split a
    // localized name or preview when reserving the final ASCII ellipsis.
    if (truncated)
        while (prefix && (static_cast<unsigned char>(text[prefix]) & 0xc0U) == 0x80U) --prefix;
    const char* suffix = truncated ? "..." : "";
    const size_t suffix_size = truncated ? 3 : 0;
    // DOT mode mutates LVGL's own text. Compare our bounded source cache;
    // passing the full message would force long layout work on every refresh.
    if (std::strlen(cached.data()) == prefix + suffix_size &&
        !std::memcmp(cached.data(), text, prefix) && !std::strcmp(cached.data() + prefix, suffix)) return;
    std::memcpy(cached.data(), text, prefix);
    std::memcpy(cached.data() + prefix, suffix, suffix_size + 1);
    ::ui::i18n::set_label_text_raw(label, cached.data());
}

void set_label_text_if_changed_raw(lv_obj_t* label, const char* text)
{
    if (label == nullptr || text == nullptr)
    {
        return;
    }
    const char* current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, text) != 0)
    {
        ::ui::i18n::set_label_text_raw(label, text);
    }
}

void set_label_text_if_changed(lv_obj_t* label, const char* english)
{
    const char* localized = ::ui::i18n::tr(english);
    if (label == nullptr || localized == nullptr)
    {
        return;
    }
    const char* current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, localized) != 0)
    {
        ::ui::i18n::set_label_text_raw(label, localized);
    }
}

} // namespace

void refresh_recent_widget(void* context);

WidgetHandle create_recent_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h)
{
    auto* memory = create_widget_state<RecentWidgetUi>();
    if (!memory) return {};
    auto& recent = *memory;
    recent.chrome = create_card_chrome(parent, ::ui::i18n::tr("Recent Activity"), card_w, card_h);

    const lv_coord_t body_w = lv_obj_get_content_width(recent.chrome.body);
    const lv_coord_t body_h = lv_obj_get_content_height(recent.chrome.body);
    const lv_coord_t gap = 6;
    const lv_coord_t row_h = std::max<lv_coord_t>(26, (body_h - gap) / static_cast<lv_coord_t>(kVisibleRows));

    for (size_t i = 0; i < kVisibleRows; ++i)
    {
        recent.rows[i] = lv_obj_create(recent.chrome.body);
        lv_obj_set_size(recent.rows[i], body_w, row_h);
        lv_obj_set_pos(recent.rows[i], 0, static_cast<lv_coord_t>(i * (row_h + gap)));
        lv_obj_set_style_bg_color(recent.rows[i], i == 0 ? color_soft_amber() : lv_color_hex(0xF6E7C5), 0);
        lv_obj_set_style_bg_opa(recent.rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(recent.rows[i], 0, 0);
        lv_obj_set_style_radius(recent.rows[i], 14, 0);
        lv_obj_set_style_pad_left(recent.rows[i], 10, 0);
        lv_obj_set_style_pad_right(recent.rows[i], 10, 0);
        lv_obj_set_style_pad_top(recent.rows[i], 4, 0);
        lv_obj_set_style_pad_bottom(recent.rows[i], 4, 0);
        lv_obj_clear_flag(recent.rows[i], LV_OBJ_FLAG_SCROLLABLE);

        recent.row_dots[i] = lv_obj_create(recent.rows[i]);
        lv_obj_set_size(recent.row_dots[i], 10, 10);
        lv_obj_set_style_radius(recent.row_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(recent.row_dots[i], i == 0 ? color_amber_dark() : color_info(), 0);
        lv_obj_set_style_bg_opa(recent.row_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(recent.row_dots[i], 0, 0);
        lv_obj_align(recent.row_dots[i], LV_ALIGN_LEFT_MID, 0, 0);

        recent.row_names[i] = lv_label_create(recent.rows[i]);
        style_body_label(recent.row_names[i], &lv_font_montserrat_12, color_text());
        lv_obj_set_width(recent.row_names[i], static_cast<lv_coord_t>(body_w - 80));
        lv_obj_align(recent.row_names[i], LV_ALIGN_TOP_LEFT, 18, 1);
        lv_label_set_long_mode(recent.row_names[i], LV_LABEL_LONG_DOT);
        lv_obj_set_height(recent.row_names[i], lv_font_get_line_height(&lv_font_montserrat_12));
        ::ui::i18n::set_label_text_raw(recent.row_names[i], "");

        recent.row_previews[i] = lv_label_create(recent.rows[i]);
        style_body_label(recent.row_previews[i], &lv_font_montserrat_12, color_text_dim());
        lv_obj_set_width(recent.row_previews[i], static_cast<lv_coord_t>(body_w - 80));
        lv_obj_align(recent.row_previews[i], LV_ALIGN_BOTTOM_LEFT, 18, -1);
        lv_label_set_long_mode(recent.row_previews[i], LV_LABEL_LONG_DOT);
        lv_obj_set_height(recent.row_previews[i], (body_h < 132 ? 1 : 2) * lv_font_get_line_height(&lv_font_montserrat_12));
        ::ui::i18n::set_label_text_raw(recent.row_previews[i], "");

        recent.row_badges[i] = lv_obj_create(recent.rows[i]);
        lv_obj_set_size(recent.row_badges[i], LV_SIZE_CONTENT, 16);
        lv_obj_align(recent.row_badges[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(recent.row_badges[i], 8, 0);
        lv_obj_set_style_bg_color(recent.row_badges[i], color_warn(), 0);
        lv_obj_set_style_bg_opa(recent.row_badges[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(recent.row_badges[i], 0, 0);
        lv_obj_set_style_pad_left(recent.row_badges[i], 5, 0);
        lv_obj_set_style_pad_right(recent.row_badges[i], 5, 0);
        lv_obj_set_style_pad_top(recent.row_badges[i], 1, 0);
        lv_obj_set_style_pad_bottom(recent.row_badges[i], 1, 0);
        lv_obj_add_flag(recent.row_badges[i], LV_OBJ_FLAG_HIDDEN);

        recent.row_badge_labels[i] = lv_label_create(recent.row_badges[i]);
        style_body_label(recent.row_badge_labels[i], &lv_font_montserrat_12, lv_color_white());
        lv_obj_center(recent.row_badge_labels[i]);
    }

    recent.scan_track = lv_obj_create(recent.chrome.footer);
    lv_obj_set_size(recent.scan_track, 78, 8);
    lv_obj_align(recent.scan_track, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(recent.scan_track, color_line(), 0);
    lv_obj_set_style_bg_opa(recent.scan_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(recent.scan_track, 0, 0);
    lv_obj_set_style_radius(recent.scan_track, 4, 0);
    lv_obj_clear_flag(recent.scan_track, LV_OBJ_FLAG_SCROLLABLE);

    recent.scan_runner = lv_obj_create(recent.scan_track);
    lv_obj_set_size(recent.scan_runner, 20, 8);
    lv_obj_set_style_bg_color(recent.scan_runner, color_amber_dark(), 0);
    lv_obj_set_style_bg_opa(recent.scan_runner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(recent.scan_runner, 0, 0);
    lv_obj_set_style_radius(recent.scan_runner, 4, 0);

    recent.footer_label = lv_label_create(recent.chrome.footer);
    style_footer_label(recent.footer_label);
    lv_obj_align(recent.footer_label, LV_ALIGN_RIGHT_MID, 0, 0);
    ::ui::i18n::set_label_text(recent.footer_label, "no recent traffic");
    return own_widget(recent.chrome.card, memory, refresh_recent_widget, 2000);
}

void refresh_recent_widget(void* context)
{
    auto& recent = *static_cast<RecentWidgetUi*>(context);
    if (recent.chrome.card == nullptr)
    {
        return;
    }

    size_t total = 0;
    const auto conversations = app::messagingFacade().getChatService().getConversations(0, 3, &total);

    set_status_chip(recent.chrome,
                    total > 0 ? "LIVE" : "IDLE",
                    total > 0 ? color_soft_blue() : color_soft_amber(),
                    total > 0 ? color_info() : color_text());

    for (size_t i = 0; i < kVisibleRows; ++i)
    {
        if (i < conversations.size())
        {
            const auto& conv = conversations[i];
            // Let LVGL elide at a UTF-8 character boundary using actual pixels.
            // Byte-count substrings can split a localized name or message.
            set_recent_text(recent.row_names[i], recent.row_name_text[i], conv.name.c_str());
            set_recent_text(recent.row_previews[i], recent.row_preview_text[i], conv.preview.empty() ? ::ui::i18n::tr("No preview") : conv.preview.c_str());
            lv_obj_set_style_bg_color(recent.row_dots[i], conv.unread > 0 ? color_warn() : color_info(), 0);

            if (conv.unread > 0)
            {
                char unread_buf[12];
                std::snprintf(unread_buf, sizeof(unread_buf), "%d", conv.unread);
                set_label_text_if_changed(recent.row_badge_labels[i], unread_buf);
                lv_obj_clear_flag(recent.row_badges[i], LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(recent.row_badges[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        else
        {
            set_recent_text(recent.row_names[i], recent.row_name_text[i], i == 0 ? ::ui::i18n::tr("No recent traffic") : "");
            set_recent_text(recent.row_previews[i], recent.row_preview_text[i], i == 0 ? ::ui::i18n::tr("Messages and broadcasts show here") : "");
            lv_obj_add_flag(recent.row_badges[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(recent.row_dots[i], color_line(), 0);
        }
    }

    char footer[64];
    std::snprintf(
        footer, sizeof(footer), "%s", ::ui::i18n::format("%u threads active", static_cast<unsigned>(total)).c_str());
    set_label_text_if_changed_raw(recent.footer_label, footer);

    // Keep the footer accent subtle; continuously animating it forces a redraw
    // every dashboard tick even when no conversation state changed.
    const lv_coord_t track_w = lv_obj_get_width(recent.scan_track);
    const lv_coord_t runner_w = lv_obj_get_width(recent.scan_runner);
    const lv_coord_t travel = track_w - runner_w;
    const lv_coord_t x = travel > 0 ? travel / 2 : 0;
    if (lv_obj_get_x(recent.scan_runner) != x)
    {
        lv_obj_set_x(recent.scan_runner, x);
    }
}

} // namespace ui::menu::dashboard
