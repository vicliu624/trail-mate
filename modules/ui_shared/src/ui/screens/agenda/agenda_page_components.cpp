#include "ui/screens/agenda/agenda_page_components.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_editor_runtime.h"
#include "ui/screens/agenda/agenda_page_input.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar_power_presenter.h"

#include "ui_presentation/agenda/agenda_countdown.h"
#include <cstdio>
#include <cstring>
#include <new>

namespace ui::agenda::page::components
{
namespace
{
void plain(lv_obj_t* object)
{
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::page_bg(), 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, bool translated = true, bool caption = false)
{
    auto* value = lv_label_create(parent);
    const auto* font = caption ? ::ui::page_profile::resolve_caption_font() : ::ui::page_profile::resolve_body_font();
    const char* display = translated ? ::ui::i18n::tr(text) : text;
    lv_label_set_text(value, display);
    if (translated) ::ui::fonts::apply_localized_font(value, display, font);
    else ::ui::fonts::apply_content_font(value, display, font);
    lv_obj_set_style_text_color(value, ::ui::theme::text(), 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    return value;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, Action action, uint8_t row = 0)
{
    auto* object = lv_button_create(parent);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::accent(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 2, 0);
    lv_obj_set_style_radius(object, 3, 0);
    const auto encoded = static_cast<uintptr_t>(action) | (static_cast<uintptr_t>(row) << 8);
    lv_obj_add_event_cb(object, input::activate, LV_EVENT_CLICKED, reinterpret_cast<void*>(encoded));
    input::bind(object);
    if (text)
    {
        auto* text_label = label(object, text);
        lv_obj_set_width(text_label, LV_PCT(100));
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(text_label);
    }
    if (state()->awaiting_command) lv_obj_add_state(object, LV_STATE_DISABLED);
    return object;
}

void date_text(int64_t seconds, char* text, std::size_t size, bool time_only)
{
    ::agenda::CivilTime civil;
    if (!::agenda::fromCalendarSeconds(seconds, civil))
    {
        text[0] = '\0';
        return;
    }
    if (time_only) std::snprintf(text, size, "%02u:%02u", civil.hour, civil.minute);
    else std::snprintf(text, size, "%04u-%02u-%02u", civil.year, civil.month, civil.day);
}

void home(lv_coord_t width, lv_coord_t height)
{
    auto& s = *state();
    const auto& profile = ::ui::page_profile::current();
    const bool compact = profile.variant == ::ui::page_profile::LayoutVariant::EncoderCompact;
    const int row_height = compact ? 24 : 32;
    const int section_height = compact ? 17 : 20;
    const int footer_height = profile.control_button_height;
    const int content_height = height - footer_height - 4;
    int y = 0;
    int64_t previous_day = -1;
    uint8_t shown = 0;
    const char* message = !s.snapshot.storage_ready                        ? "Agenda storage unavailable"
                          : !s.snapshot.clock_valid                        ? "Set the device time first"
                          : s.snapshot.result != ::agenda::StoreResult::Ok ? "Could not read events"
                          : s.snapshot.page.count == 0                     ? "No upcoming events"
                                                                           : nullptr;
    if (message)
    {
        auto* text = label(s.body, message);
        lv_obj_set_width(text, width - 8);
        lv_obj_set_pos(text, 4, 6);
    }
    else
        for (uint8_t i = 0; i < s.snapshot.page.count; ++i)
        {
            const auto& entry = s.snapshot.page.rows[i];
            const int64_t day = entry.occurrence.start / 86400 * 86400;
            const bool section = day != previous_day;
            if (y + row_height + (section ? section_height : 0) > content_height) break;
            if (section)
            {
                char date[16];
                date_text(day, date, sizeof(date), false);
                const char* title = day == s.snapshot.today_start           ? "Today"
                                    : day == s.snapshot.today_start + 86400 ? "Tomorrow"
                                                                            : date;
                auto* heading = label(s.body, title, title != date, true);
                lv_obj_set_pos(heading, 4, y);
                lv_obj_set_width(heading, width - 8);
                y += section_height;
                previous_day = day;
            }
            auto* row = button(s.body, nullptr, Action::OpenRow, i);
            lv_obj_set_pos(row, 2, y);
            lv_obj_set_size(row, width - 4, row_height);
            char time[8];
            date_text(entry.occurrence.start, time, sizeof(time), true);
            auto* time_label = label(row, time, false);
            lv_obj_set_size(time_label, 48, LV_SIZE_CONTENT);
            lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 2, 0);
            const int countdown_width = compact ? 100 : 84;
            auto* title = label(row, entry.title, false);
            lv_obj_set_width(title, width - 64 - countdown_width);
            lv_obj_align(title, LV_ALIGN_LEFT_MID, 54, 0);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(i + 1)));
            const auto now = s.host->source->currentTime();
            char text[32];
            formatCountdown(entry.occurrence.start, now.calendar_seconds, now.valid, text, sizeof(text));
            auto* countdown = label(row, text, false, true);
            lv_obj_set_width(countdown, countdown_width - 4);
            lv_obj_set_style_text_align(countdown, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(countdown, LV_ALIGN_RIGHT_MID, -2, 0);
            y += row_height;
            ++shown;
        }
    if (!message && shown < s.snapshot.page.count)
    {
        // Pagination resumes after the last row actually rendered, not after
        // hidden rows whose section headings no longer fit the framebuffer.
        s.snapshot.page.count = shown;
        s.snapshot.page.has_more = true;
    }
    const int segment = width / 4;
    auto* create = button(s.body, "New", Action::NewEvent);
    lv_obj_set_size(create, segment - 3, footer_height);
    lv_obj_set_pos(create, 1, height - footer_height);
    if (!s.snapshot.clock_valid || !s.snapshot.storage_ready) lv_obj_add_state(create, LV_STATE_DISABLED);
    auto* today = button(s.body, "Today", Action::Today);
    lv_obj_set_size(today, segment - 3, footer_height);
    lv_obj_set_pos(today, segment + 1, height - footer_height);
    auto* date = button(s.body, "Date", Action::JumpToDate);
    lv_obj_set_size(date, segment - 3, footer_height);
    lv_obj_set_pos(date, segment * 2 + 1, height - footer_height);
    auto* next = button(s.body, "More", Action::NextPage);
    lv_obj_set_size(next, segment - 3, footer_height);
    lv_obj_set_pos(next, segment * 3 + 1, height - footer_height);
    if (!s.snapshot.page.has_more) lv_obj_add_state(next, LV_STATE_DISABLED);
}

void scroll_detail(lv_event_t* event)
{
    auto* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const auto key = lv_event_get_key(event);
    const bool down = key == LV_KEY_DOWN;
    const bool up = key == LV_KEY_UP;
    if ((!down && !up) || (down && lv_obj_get_scroll_bottom(object) <= 0) ||
        (up && lv_obj_get_scroll_top(object) <= 0)) return;
    lv_obj_scroll_by(object, 0, down ? -24 : 24, LV_ANIM_OFF);
    lv_event_stop_processing(event);
    lv_event_stop_bubbling(event);
}

void detail(lv_coord_t width, lv_coord_t height)
{
    auto& s = *state();
    const auto& event = s.draft.event;
    auto* title = label(s.body, event.title, false);
    lv_obj_set_pos(title, 4, 2);
    lv_obj_set_width(title, width - 8);
    lv_obj_set_style_text_color(title, ::ui::theme::accent(), 0);
    ::ui::fonts::apply_content_font(title, event.title, ::ui::page_profile::resolve_title_font());
    if (s.confirm_delete)
    {
        auto* prompt = label(s.body, "Delete this event?");
        lv_obj_set_pos(prompt, 4, 36);
        lv_obj_set_width(prompt, width - 8);
        auto* cancel = button(s.body, "Cancel", Action::CancelDelete);
        auto* confirm = button(s.body, "Delete", Action::ConfirmDelete);
        const int h = ::ui::page_profile::resolve_control_button_height();
        lv_obj_set_size(cancel, width / 2 - 6, h);
        lv_obj_set_size(confirm, width / 2 - 6, h);
        lv_obj_set_pos(cancel, 3, height - h);
        lv_obj_set_pos(confirm, width / 2 + 3, height - h);
        lv_group_focus_obj(cancel);
        return;
    }
    const auto& occurrence = s.detail_occurrence;
    char date[16], start[8], end[8], when[48];
    date_text(occurrence.start, date, sizeof(date), false);
    date_text(occurrence.start, start, sizeof(start), true);
    date_text(occurrence.end, end, sizeof(end), true);
    if (occurrence.has_end) std::snprintf(when, sizeof(when), "%s  %s - %s", date, start, end);
    else std::snprintf(when, sizeof(when), "%s  %s", date, start);
    auto* date_label = label(s.body, when, false, true);
    lv_obj_set_pos(date_label, 4, 28);
    lv_obj_set_width(date_label, width - 8);
    const int footer = ::ui::page_profile::resolve_control_button_height();
    const bool located = (event.flags & ::agenda::HasLocation) != 0;
    const bool wide = width >= 400;
    auto* content = lv_obj_create(s.body);
    plain(content);
    lv_obj_set_pos(content, 4, 50);
    lv_obj_set_size(content, width - 8, height - 54 - footer);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_add_event_cb(content, scroll_detail, LV_EVENT_KEY, nullptr);
    input::bind(content);
    const int text_width = located && wide ? (width - 20) / 2 : width - 16;
    int y = 0;
    if (located)
    {
        const int map_width = wide ? (width - 20) / 2 : width - 16;
        const int map_height = wide ? height - 58 - footer : 92;
        auto* map_host = lv_obj_create(content);
        plain(map_host);
        lv_obj_set_size(map_host, map_width, map_height);
        lv_obj_set_pos(map_host, wide ? text_width + 4 : 0, 0);
        s.detail_map = new (std::nothrow)::ui::widgets::map::Runtime;
        if (s.detail_map)
        {
            namespace map = ::ui::widgets::map;
            map::create(*s.detail_map, map_host, 180);
            map::set_size(*s.detail_map, map_width, map_height);
            map::set_gesture_enabled(*s.detail_map, false);
            map::Model model;
            model.zoom = 16;
            model.focus_point = {true, event.latitude_e7 / 1e7, event.longitude_e7 / 1e7};
            map::apply_model(*s.detail_map, model);
        }
        auto* marker = lv_obj_create(map_host);
        plain(marker);
        lv_obj_set_size(marker, 12, 12);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(marker, ::ui::theme::accent(), 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(marker, 2, 0);
        lv_obj_set_style_border_color(marker, ::ui::theme::text(), 0);
        lv_obj_center(marker);
        s.map_notice = label(map_host, "Map - No Map Data", true, true);
        lv_obj_set_width(s.map_notice, map_width - 4);
        lv_obj_align(s.map_notice, LV_ALIGN_BOTTOM_MID, 0, -2);
        if (!wide) y = map_height + 8;
    }
    if (event.flags & ::agenda::HasNote)
    {
        auto* note = label(content, event.note, false);
        lv_obj_set_pos(note, 0, y);
        lv_obj_set_width(note, text_width);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_update_layout(note);
        y += lv_obj_get_height(note) + 8;
    }
    const auto metadata = [&](const char* name, const char* value)
    {
        auto* caption = label(content, name, true, true);
        lv_obj_set_pos(caption, 0, y);
        lv_obj_set_width(caption, text_width);
        lv_obj_set_style_text_opa(caption, LV_OPA_60, 0);
        auto* text = label(content, value, true, true);
        lv_obj_set_pos(text, 0, y + 16);
        lv_obj_set_width(text, text_width);
        y += 38;
    };
    if (event.flags & ::agenda::HasReminder) metadata("Reminder", editor::reminderText(event));
    if (event.repeat != ::agenda::Repeat::None) metadata("Repeat", editor::repeatText(event.repeat));
    auto* remove = button(s.body, "Delete", Action::Delete);
    auto* edit = button(s.body, "Edit", Action::EditEvent);
    const int h = ::ui::page_profile::resolve_control_button_height();
    const bool can_navigate = (event.flags & ::agenda::HasLocation) && s.host->request_target;
    const int segment = width / (can_navigate ? 3 : 2);
    lv_obj_set_size(edit, segment - 6, h);
    lv_obj_set_pos(edit, 3, height - h);
    lv_obj_set_size(remove, segment - 6, h);
    lv_obj_set_pos(remove, segment + 3, height - h);
    if (can_navigate)
    {
        auto* navigate = button(s.body, "Navigate", Action::Navigate);
        lv_obj_set_size(navigate, segment - 6, h);
        lv_obj_set_pos(navigate, segment * 2 + 3, height - h);
        lv_obj_set_style_bg_color(navigate, ::ui::theme::accent(), 0);
    }
}
} // namespace

lv_obj_t* addLabel(lv_obj_t* parent, const char* text, bool translated, bool caption)
{
    return label(parent, text, translated, caption);
}
lv_obj_t* addButton(lv_obj_t* parent, const char* text, Action action, uint8_t value)
{
    return button(parent, text, action, value);
}

void create(lv_obj_t* parent)
{
    auto& s = *state();
    s.root = lv_obj_create(parent);
    plain(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    ::ui::widgets::top_bar_init(s.top_bar, s.root);
    ::ui::widgets::top_bar_set_back_callback(
        s.top_bar, [](void*)
        { runtime::request(Action::Back); },
        nullptr);
    s.body = lv_obj_create(s.root);
    plain(s.body);
    const int top = ::ui::page_profile::current().top_bar_height;
    lv_obj_set_pos(s.body, 0, top);
    lv_obj_set_size(s.body, LV_PCT(100), LV_PCT(100));
    lv_obj_update_layout(s.root);
    lv_obj_set_height(s.body, lv_obj_get_height(s.root) - top);
}
void render()
{
    auto& s = *state();
    if (s.view == View::Editor && (s.confirm_discard || s.editor_widgets.confirm_root))
    {
        // A small discard confirmation preserves the live textareas, including
        // invalid oversized input, so choosing Keep editing loses no text.
        editor::updateDiscard();
        return;
    }
    delete s.detail_map;
    s.detail_map = nullptr;
    s.map_notice = nullptr;
    lv_group_remove_all_objs(s.group);
    lv_obj_clean(s.body);
    s.editor_widgets = {};
    input::bind(s.top_bar.back_btn);
    const bool editing = s.view != View::Agenda && s.view != View::Detail;
    ::ui::widgets::top_bar_set_title(s.top_bar, ::ui::i18n::tr(editing ? editor::title() : s.view == View::Agenda ? "Agenda"
                                                                                                                  : "Event Detail"));
    const int width = lv_obj_get_width(s.body);
    int height = lv_obj_get_height(s.body);
    if (!editing && (s.error || s.awaiting_command))
    {
        auto* status = label(s.body, s.awaiting_command ? "Saving..." : s.error, true, true);
        lv_obj_set_width(status, width - 8);
        lv_obj_set_pos(status, 4, height - 20);
        height -= 22;
    }
    if (editing) editor::render(width, height);
    else if (s.view == View::Agenda) home(width, height);
    else detail(width, height);
    if (!s.confirm_delete) lv_group_focus_obj(s.editor_widgets.focus ? s.editor_widgets.focus : s.top_bar.back_btn);
    lv_group_set_editing(s.group, false);
}

void updateCountdowns()
{
    auto* s = state();
    if (s && s->detail_map && s->map_notice)
    {
        const bool visible = ::ui::widgets::map::status(*s->detail_map).has_visible_map_data;
        if (visible) lv_obj_add_flag(s->map_notice, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s->map_notice, LV_OBJ_FLAG_HIDDEN);
    }
    if (!s || s->view != View::Agenda || !s->body) return;
    const auto now = s->host->source->currentTime();
    for (uint32_t i = 0; i < lv_obj_get_child_count(s->body); ++i)
    {
        auto* row = lv_obj_get_child(s->body, i);
        const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row));
        if (!index || index > s->snapshot.page.count || lv_obj_get_child_count(row) != 3) continue;
        auto* value = lv_obj_get_child(row, 2);
        char text[32];
        formatCountdown(s->snapshot.page.rows[index - 1].occurrence.start,
                        now.calendar_seconds, now.valid, text, sizeof(text));
        if (std::strcmp(text, lv_label_get_text(value)) != 0) lv_label_set_text(value, text);
    }
}
void destroy()
{
    auto& s = *state();
    delete s.detail_map;
    s.detail_map = nullptr;
    s.map_notice = nullptr;
    ::ui::widgets::top_bar_power::unbind(s.top_bar);
    if (s.root) lv_obj_delete(s.root);
    s.root = s.body = nullptr;
    s.top_bar = {};
}
} // namespace ui::agenda::page::components
