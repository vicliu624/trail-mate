#include "ui/components/map_poi_search.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include <algorithm>
#include <cstdio>
#include <new>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::components::map_poi_search
{
namespace
{
namespace search = platform::ui::map_search;
constexpr size_t kPageRows = 6;
struct State
{
    lv_obj_t* overlay = nullptr;
    lv_obj_t* input = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* rows[kPageRows]{};
    lv_obj_t* labels[kPageRows]{};
    lv_obj_t* details[kPageRows]{};
    lv_obj_t* page_label = nullptr;
    lv_obj_t* previous = nullptr;
    lv_obj_t* next = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* restore = nullptr;
    lv_timer_t* timer = nullptr;
    widgets::ImeWidget ime;
    search::Snapshot snapshot{};
    search::Result results[search::kMaxResults]{};
    char text[224]{};
    size_t page = 0;
    uint32_t font_refresh_at = 0;
    double latitude = 0, longitude = 0;
    Selection selection = nullptr;
    void* context = nullptr;
};
State* s_state = nullptr;
bool activate(lv_event_t* event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) return true;
    if (lv_event_get_code(event) != LV_EVENT_KEY || lv_event_get_key(event) != LV_KEY_ENTER) return false;
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}
void render()
{
    auto& state = *s_state;
    const auto& snapshot = state.snapshot;
    state.page = std::min(state.page, snapshot.count ? (snapshot.count - 1) / kPageRows : size_t{0});
    for (size_t row = 0; row < kPageRows; ++row)
    {
        const size_t index = state.page * kPageRows + row;
        if (index >= snapshot.count)
        {
            lv_obj_add_flag(state.rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const auto& result = state.results[index];
        i18n::set_content_label_text_raw(state.labels[row], result.name);
        if (result.distance_m < 1000)
            std::snprintf(state.text, sizeof(state.text), "%.0f m from map center", result.distance_m);
        else
            std::snprintf(state.text, sizeof(state.text), "%.1f km from map center", result.distance_m / 1000);
        i18n::set_label_text_raw(state.details[row], state.text);
        lv_obj_clear_flag(state.rows[row], LV_OBJ_FLAG_HIDDEN);
    }
    if (state.page) lv_obj_clear_state(state.previous, LV_STATE_DISABLED);
    else lv_obj_add_state(state.previous, LV_STATE_DISABLED);
    if ((state.page + 1) * kPageRows < snapshot.count) lv_obj_clear_state(state.next, LV_STATE_DISABLED);
    else lv_obj_add_state(state.next, LV_STATE_DISABLED);
    std::snprintf(state.text, sizeof(state.text), "%u / %u", static_cast<unsigned>(state.page + 1),
                  static_cast<unsigned>(std::max(size_t{1}, (snapshot.count + kPageRows - 1) / kPageRows)));
    i18n::set_label_text_raw(state.page_label, state.text);
    const char* message = "Enter a POI name";
    if (snapshot.status == search::Status::Searching) message = "Searching all maps...";
    else if (snapshot.status == search::Status::NoMaps) message = "No searchable TMAP maps";
    else if (snapshot.status == search::Status::Error || snapshot.incomplete) message = "Some maps could not be searched";
    else if (snapshot.status == search::Status::Ready)
        message = snapshot.count ? snapshot.limited ? "Best 24 results; refine your search" : "Select a place to show on map" : "No results";
    i18n::set_label_text(state.status, message);
}
void poll(lv_timer_t*)
{
    if (s_state && search::poll(s_state->snapshot, s_state->results, search::kMaxResults)) render();
    if (!s_state || lv_tick_elaps(s_state->font_refresh_at) < 500) return;
    s_state->font_refresh_at = lv_tick_get();
    // Content fonts load after an LVGL frame and may retry when SD is busy.
    // Rebind visible names even when the search snapshot has not changed.
    for (size_t row = 0; row < kPageRows; ++row)
    {
        const size_t index = s_state->page * kPageRows + row;
        if (index < s_state->snapshot.count)
            fonts::apply_content_font(s_state->labels[row], s_state->results[index].name, &lv_font_montserrat_14);
    }
}
void run(lv_event_t* event)
{
    if (!s_state || !activate(event)) return;
    const char* text = lv_textarea_get_text(s_state->input);
    if (!text || !text[0])
    {
        i18n::set_label_text(s_state->status, "Enter a POI name");
        return;
    }
    if (!search::submit(text, s_state->latitude, s_state->longitude))
    {
        i18n::set_label_text(s_state->status, "Search unavailable or invalid query");
        return;
    }
    s_state->page = 0;
    poll(nullptr);
}
void cancel_event(lv_event_t* event)
{
    if (activate(event)) close();
}
void page_event(lv_event_t* event)
{
    if (!s_state || !activate(event)) return;
    const bool forward = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) != 0;
    if (forward && (s_state->page + 1) * kPageRows < s_state->snapshot.count) ++s_state->page;
    else if (!forward && s_state->page) --s_state->page;
    render();
}
void selected(lv_event_t* event)
{
    if (!s_state || !activate(event)) return;
    const size_t row = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const size_t index = s_state->page * kPageRows + row;
    if (index >= s_state->snapshot.count) return;
    // The callback consumes the borrowed record before close frees its PSRAM.
    if (s_state->selection) s_state->selection(s_state->results[index], s_state->context);
    close();
}
void key(lv_event_t* event)
{
    if (!s_state || lv_event_get_code(event) != LV_EVENT_KEY) return;
    if (lv_event_get_key(event) == LV_KEY_ESC)
    {
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        close();
        return;
    }
    if (lv_event_get_target(event) == s_state->input) s_state->ime.handle_key(event);
    lv_event_stop_bubbling(event);
}
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback, uintptr_t data = 0)
{
    auto* object = lv_btn_create(parent);
    lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
    two_pane_styles::apply_btn_basic(object);
    lv_obj_set_style_bg_color(object, lv_color_hex(0xffd59a), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(object, lv_color_hex(0xd47a12), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(object, LV_SIZE_CONTENT, 26);
    auto* label = lv_label_create(object);
    i18n::set_label_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, reinterpret_cast<void*>(data));
    lv_obj_add_event_cb(object, callback, LV_EVENT_KEY, reinterpret_cast<void*>(data));
    lv_group_add_obj(s_state->group, object);
    return object;
}
} // namespace

bool open(lv_obj_t* parent, double latitude, double longitude, Selection selection, void* context)
{
    if (s_state)
    {
        lv_group_focus_obj(s_state->input);
        return true;
    }
    if (!parent || !lv_obj_is_valid(parent)) return false;
#if defined(ESP_PLATFORM)
    void* storage = heap_caps_aligned_alloc(alignof(State), sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* storage = ::operator new(sizeof(State), std::nothrow);
#endif
    if (!storage) return false;
    s_state = new (storage) State{};
    auto& state = *s_state;
    state.latitude = latitude;
    state.longitude = longitude;
    state.selection = selection;
    state.context = context;
    state.restore = lv_group_get_default();
    state.group = lv_group_create();
    if (!state.group)
    {
        close();
        return false;
    }
    set_default_group(state.group);
    state.overlay = lv_obj_create(parent);
    lv_obj_set_size(state.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(state.overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(state.overlay, LV_ALIGN_CENTER, 0, 0);
    two_pane_styles::apply_container_main(state.overlay);
    lv_obj_set_style_bg_opa(state.overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(state.overlay, lv_color_hex(0xfff8ee), LV_PART_MAIN);
    lv_obj_set_style_text_color(state.overlay, lv_color_hex(0x30261c), LV_PART_MAIN);
    lv_obj_set_style_radius(state.overlay, 0, 0);
    lv_obj_set_style_pad_all(state.overlay, 8, 0);
    lv_obj_set_flex_flow(state.overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state.overlay, 3, 0);
    lv_obj_clear_flag(state.overlay, LV_OBJ_FLAG_SCROLLABLE);
    auto* title = lv_label_create(state.overlay);
    i18n::set_label_text(title, "Find a place");
    auto* input_row = lv_obj_create(state.overlay);
    lv_obj_add_flag(input_row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(input_row, LV_PCT(100), 30);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);
    state.input = lv_textarea_create(input_row);
    lv_textarea_set_one_line(state.input, true);
    lv_textarea_set_max_length(state.input, 48);
    lv_textarea_set_placeholder_text(state.input, "Place name");
    lv_obj_set_height(state.input, 28);
    lv_obj_set_flex_grow(state.input, 1);
    lv_group_add_obj(state.group, state.input);
    lv_obj_add_event_cb(state.input, key, LV_EVENT_KEY, nullptr);
    button(input_row, "Search", run);
    state.ime.init(state.overlay, state.input);
    widgets::attach_touch_text_editor(state.input, &state.ime);
    if (state.ime.toggle_btn()) lv_group_add_obj(state.group, state.ime.toggle_btn());
    state.status = lv_label_create(state.overlay);
    lv_obj_set_width(state.status, LV_PCT(100));
    lv_label_set_long_mode(state.status, LV_LABEL_LONG_DOT);
    state.list = lv_obj_create(state.overlay);
    lv_obj_add_flag(state.list, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_width(state.list, LV_PCT(100));
    lv_obj_set_height(state.list, 0);
    lv_obj_set_flex_grow(state.list, 1);
    lv_obj_set_flex_flow(state.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(state.list, 2, 0);
    lv_obj_set_style_bg_opa(state.list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(state.list, lv_color_hex(0xfff8ee), 0);
    lv_obj_set_style_border_width(state.list, 0, 0);
    for (size_t row = 0; row < kPageRows; ++row)
    {
        state.rows[row] = button(state.list, "", selected, row);
        lv_obj_set_size(state.rows[row], LV_PCT(100), 50);
        lv_obj_set_style_pad_all(state.rows[row], 4, 0);
        lv_obj_set_flex_flow(state.rows[row], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(state.rows[row], 2, 0);
        lv_obj_set_style_bg_color(state.rows[row], lv_color_hex(0xffffff), 0);
        state.labels[row] = lv_obj_get_child(state.rows[row], 0);
        lv_obj_set_width(state.labels[row], LV_PCT(100));
        lv_obj_set_height(state.labels[row], LV_SIZE_CONTENT);
        lv_label_set_long_mode(state.labels[row], LV_LABEL_LONG_DOT);
        state.details[row] = lv_label_create(state.rows[row]);
        lv_obj_set_width(state.details[row], LV_PCT(100));
        lv_label_set_long_mode(state.details[row], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(state.details[row], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(state.details[row], lv_color_hex(0x75695e), 0);
        lv_obj_add_flag(state.rows[row], LV_OBJ_FLAG_HIDDEN);
    }
    auto* footer = lv_obj_create(state.overlay);
    lv_obj_add_flag(footer, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(footer, LV_PCT(100), 28);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    state.previous = button(footer, "<", page_event);
    state.page_label = lv_label_create(footer);
    lv_obj_set_style_pad_top(state.page_label, 5, 0);
    state.next = button(footer, ">", page_event, 1);
    button(footer, "Close", cancel_event);
    lv_obj_add_event_cb(state.overlay, key, LV_EVENT_KEY, nullptr);
    state.timer = lv_timer_create(poll, 150, nullptr);
    render();
    lv_group_focus_obj(state.input);
    state.ime.activate();
    lv_obj_move_foreground(state.overlay);
    return true;
}
void close()
{
    if (!s_state) return;
    auto* state = s_state;
    s_state = nullptr;
    search::cancel();
    if (state->timer) lv_timer_del(state->timer);
    state->ime.detach();
    if (state->overlay && lv_obj_is_valid(state->overlay)) lv_obj_del(state->overlay);
    if (state->restore) set_default_group(state->restore);
    if (state->group) lv_group_del(state->group);
    state->~State();
#if defined(ESP_PLATFORM)
    heap_caps_free(state);
#else
    ::operator delete(state);
#endif
}
} // namespace ui::components::map_poi_search
