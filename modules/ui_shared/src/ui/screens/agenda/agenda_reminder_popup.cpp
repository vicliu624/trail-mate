#include "ui/screens/agenda/agenda_reminder_popup.h"

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_theme.h"

#include <algorithm>
#include <cstdio>
#include <new>

namespace ui::agenda::reminder_popup
{
namespace
{
enum class Action : uint8_t
{
    None,
    Snooze,
    Done,
    Navigate
};
struct State
{
    Host host;
    ::agenda::EventRecord event;
    lv_obj_t* root = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* buttons[3]{};
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
    uint32_t revision = 0;
    uint32_t sequence = 0;
    uint32_t opened_at = 0;
    Action pending = Action::None;
    Action submitted = Action::None;
    bool previous_editing = false;
    bool navigation_prepared = false;
};
static_assert(sizeof(State) < 512, "Reminder popup must retain only one bounded event");
State* current = nullptr;

bool group_exists(lv_group_t* group)
{
    if (!group) return false;
    for (uint32_t i = 0; i < lv_group_get_count(); ++i)
        if (lv_group_by_index(i) == group) return true;
    return false;
}
void stop(lv_event_t* event)
{
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    auto* input = lv_event_get_code(event) == LV_EVENT_KEY ? lv_indev_active() : lv_event_get_indev(event);
    if (input) lv_indev_stop_processing(input);
}
void request(Action action)
{
    // Ignore the release/long-press that was in flight when this modal opened.
    if (!current || current->submitted != Action::None || current->pending != Action::None ||
        lv_tick_elaps(current->opened_at) < 400) return;
    current->pending = action;
}
void clicked(lv_event_t* event)
{
    request(static_cast<Action>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
    stop(event);
}
void key(lv_event_t* event)
{
    if (!current) return;
    switch (lv_event_get_key(event))
    {
    case LV_KEY_ESC:
    case LV_KEY_BACKSPACE:
        request(Action::Done);
        break;
    case LV_KEY_UP:
    case LV_KEY_LEFT:
    case LV_KEY_PREV:
        lv_group_focus_prev(current->group);
        break;
    case LV_KEY_DOWN:
    case LV_KEY_RIGHT:
    case LV_KEY_NEXT:
        lv_group_focus_next(current->group);
        break;
    default:
        return; // LVGL's button handles confirmation and CLICKED.
    }
    stop(event);
}
void deleted(lv_event_t*)
{
    if (current) current->root = nullptr;
}
lv_obj_t* label(lv_obj_t* parent, const char* text, int y, int width, bool translated, bool small = false)
{
    auto* object = lv_label_create(parent);
    const char* value = translated ? ::ui::i18n::tr(text) : text;
    lv_label_set_text(object, value);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(object, 10, y);
    lv_obj_set_width(object, width - 20);
    lv_obj_set_style_text_color(object, small ? ::ui::theme::text_muted() : ::ui::theme::text(), 0);
    const auto* font = small ? ::ui::page_profile::resolve_caption_font() : ::ui::page_profile::resolve_body_font();
    if (translated) ::ui::fonts::apply_localized_font(object, value, font);
    else ::ui::fonts::apply_content_font(object, value, font);
    return object;
}
void set_pending(bool pending)
{
    for (auto* button : current->buttons)
    {
        if (!button) continue;
        if (pending) lv_obj_add_state(button, LV_STATE_DISABLED);
        else lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
}
void status(const char* key)
{
    ::ui::i18n::set_label_text(current->status, key);
}
void button(lv_obj_t* panel, unsigned index, unsigned count, const char* text, Action action, int width, int height)
{
    const int button_width = (width - 20 - (count - 1) * 6) / count;
    // Three actions on the compact layout need room for a two-line caption.
    // Keep the footer bottom-aligned rather than shrinking the profile font.
    const int button_height = count == 3 && width < 400 ? 38 : 28;
    auto* object = lv_button_create(panel);
    current->buttons[index] = object;
    lv_obj_set_pos(object, 10 + index * (button_width + 6), height - button_height - 10);
    lv_obj_set_size(object, button_width, button_height);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_set_style_pad_all(object, 2, 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::accent(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_outline_width(object, 0, LV_STATE_FOCUS_KEY);
    const char* value = ::ui::i18n::tr(text);
    auto* caption = lv_label_create(object);
    lv_label_set_text(caption, value);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_width(caption, LV_PCT(100));
    lv_obj_set_style_text_color(caption, ::ui::theme::text(), 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::fonts::apply_localized_font(caption, value, ::ui::page_profile::resolve_caption_font());
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, clicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
    lv_obj_add_event_cb(object, key, LV_EVENT_KEY, nullptr);
    lv_group_add_obj(current->group, object);
}
bool open(const Host& host, const ReminderSnapshot& snapshot)
{
    auto* state = new (std::nothrow) State;
    if (!state) return false;
    if (host.source->detail(snapshot.reminder.event_id, state->event) != ::agenda::AgendaResult::Ok)
    {
        delete state;
        return false;
    }
    if (host.wake) host.wake();
    if (!canPresent())
    {
        delete state;
        return false;
    }
    current = state;
    state->host = host;
    state->revision = snapshot.revision;
    state->opened_at = lv_tick_get();
    state->previous_group = lv_group_get_default();
    state->previous_editing = state->previous_group && lv_group_get_editing(state->previous_group);
    state->group = lv_group_create();
    set_default_group(state->group);
    ui_set_overlay_active(true);
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) lv_indev_wait_release(input);

    state->root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(state->root);
    lv_obj_set_size(state->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state->root, ::ui::theme::text(), 0);
    lv_obj_set_style_bg_opa(state->root, LV_OPA_40, 0);
    lv_obj_remove_flag(state->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(state->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(state->root, deleted, LV_EVENT_DELETE, nullptr);
    const int width = std::min(464, static_cast<int>(lv_display_get_horizontal_resolution(nullptr)) - 16);
    const int height = std::min(width < 400 ? 224 : 198, static_cast<int>(lv_display_get_vertical_resolution(nullptr)) - 16);
    auto* panel = lv_obj_create(state->root);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_border_color(panel, ::ui::theme::accent(), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(panel);
    label(panel, "Event Reminder", 8, width, true, true);
    label(panel, state->event.title, 29, width, false);
    ::agenda::CivilTime date;
    ::agenda::fromCalendarSeconds(snapshot.reminder.occurrence_start, date);
    char time[28];
    std::snprintf(time, sizeof(time), "%04u-%02u-%02u  %02u:%02u", date.year, date.month, date.day, date.hour, date.minute);
    label(panel, time, 52, width, false);
    const bool location = (state->event.flags & ::agenda::HasLocation) != 0;
    if (location) label(panel, state->event.location_name, 75, width, false);
    if (state->event.flags & ::agenda::HasNote)
    {
        auto* note = label(panel, state->event.note, location ? 98 : 75, width, false, true);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(note, location ? 34 : 55);
    }
    state->status = label(panel, "", 136, width, true, true);
    const bool navigate = location && host.prepare_navigation && host.finish_navigation;
    const unsigned count = navigate ? 3 : 2;
    unsigned index = 0;
    if (navigate) button(panel, index++, count, "Navigate", Action::Navigate, width, height);
    button(panel, index++, count, "Snooze 10 min", Action::Snooze, width, height);
    button(panel, index, count, "Done", Action::Done, width, height);
    lv_group_focus_obj(state->buttons[0]);
    return true;
}
} // namespace

bool visible() { return current && current->root; }
bool canPresent()
{
    return lv_display_get_default() && !ui_is_interruption_app_active() && !ui_is_transition_pending() &&
           (!ui_is_overlay_active() || visible());
}
void close()
{
    if (!current) return;
    auto* state = current;
    if (state->navigation_prepared && state->host.finish_navigation)
        state->host.finish_navigation(state->host.navigation_context, false);
    const bool owns_input = lv_group_get_default() == state->group;
    if (state->root) lv_obj_delete(state->root);
    if (owns_input)
    {
        const bool restore = group_exists(state->previous_group);
        set_default_group(restore ? state->previous_group : nullptr);
        if (restore) lv_group_set_editing(state->previous_group, state->previous_editing);
        // A higher-priority interruption may already own the global marker.
        if (!ui_is_interruption_app_active()) ui_set_overlay_active(false);
    }
    lv_group_delete(state->group);
    current = nullptr;
    delete state;
}
void tick(const Host& host, bool may_present)
{
    if (!host.source || !host.actions || !host.reminders)
    {
        close();
        return;
    }
    if (current && (!canPresent() || lv_group_get_default() != current->group || !current->root))
    {
        close(); // Presentation is suspended; do not consume the reminder.
        return;
    }
    const auto snapshot = host.reminders->reminderSnapshot();
    if (current)
    {
        if (current->submitted != Action::None)
        {
            const auto result = host.source->commandResult();
            if (result.sequence == current->sequence && result.state != CommandState::Pending)
            {
                if (result.state == CommandState::Succeeded)
                {
                    const auto action = current->submitted;
                    const auto navigation = current->host.finish_navigation;
                    auto* context = current->host.navigation_context;
                    current->navigation_prepared = false;
                    close();
                    if (action == Action::Navigate && navigation) navigation(context, true);
                    return;
                }
                current->submitted = Action::None;
                if (current->navigation_prepared)
                {
                    current->host.finish_navigation(current->host.navigation_context, false);
                    current->navigation_prepared = false;
                }
                set_pending(false);
                status(result.result == ::agenda::AgendaResult::Full ? "Another reminder is already snoozed" : "Could not update reminder");
            }
        }
        if (!snapshot.reminder.valid || snapshot.revision != current->revision)
        {
            close(); // Never apply a stale popup action to a replacement.
            return;
        }
        if (current->pending != Action::None)
        {
            const auto action = current->pending;
            current->pending = Action::None;
            if (action == Action::Navigate)
            {
                const auto& navigation = current->host;
                const char* error = navigation.prepare_navigation(navigation.navigation_context,
                                                                  current->event.latitude_e7, current->event.longitude_e7);
                if (error)
                {
                    status(error);
                    return; // Do not consume a reminder when navigation cannot be prepared.
                }
                current->navigation_prepared = true;
            }
            const auto result = action == Action::Snooze ? host.actions->snoozeReminder(current->revision)
                                                         : host.actions->dismissReminder(current->revision);
            if (!result.ok)
            {
                if (current->navigation_prepared)
                {
                    current->host.finish_navigation(current->host.navigation_context, false);
                    current->navigation_prepared = false;
                }
                status("Could not update reminder");
                return;
            }
            current->sequence = host.source->commandResult().sequence;
            current->submitted = action;
            set_pending(true);
            status("Saving...");
        }
        return;
    }
    if (may_present && canPresent() && snapshot.reminder.valid) open(host, snapshot);
}
} // namespace ui::agenda::reminder_popup
