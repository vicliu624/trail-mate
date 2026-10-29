#include "ui/menu/menu_dashboard.h"
#include "platform/ui/music_runtime.h"
#include "platform/ui/orientation_runtime.h"
#include "platform/ui/quick_controls_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/ux_build_config.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_UI_P4_TOUCH) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2) || defined(ARDUINO_T_LORA_PAGER)
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_theme.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::menu::dashboard
{
namespace
{
constexpr uint32_t kDashboardTimerMs = 1000;
constexpr lv_coord_t kPagerHeight = 24;
ui::agenda::IAgendaSource* s_agenda_source = nullptr;
ui::controls::IQuickControls* s_controls_source = nullptr;
::music::IService* s_music_source = nullptr;

bool widget_supported(WidgetKind kind)
{
    return kind != WidgetKind::Compass || platform::ui::orientation::heading_supported();
}
unsigned widget_count()
{
    return static_cast<unsigned>(WidgetKind::Count) - (widget_supported(WidgetKind::Compass) ? 0U : 1U);
}
unsigned overview_count()
{
    return widget_supported(WidgetKind::Compass) ? 4U : 3U;
}
WidgetKind widget_at(unsigned index)
{
    for (unsigned value = 0; value < static_cast<unsigned>(WidgetKind::Count); ++value)
    {
        const auto kind = static_cast<WidgetKind>(value);
        if (widget_supported(kind) && index-- == 0) return kind;
    }
    return WidgetKind::Count;
}
unsigned page_count(const DashboardUi& state)
{
    return state.dock_right ? 1U + widget_count() - overview_count() : widget_count();
}

bool dashboard_enabled()
{
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2) || defined(ARDUINO_T_LORA_PAGER)
    return true;
#else
    return std::strcmp(ui::menu_profile::current().name, "t_display_p4") == 0;
#endif
}
void refresh_dashboard(lv_timer_t*)
{
    auto* state = dashboard_state();
    if (!state || !state->active || !lv_obj_is_visible(state->panel)) return;
    ++state->tick;
    const uint32_t now = lv_tick_get();
    for (auto& widget : state->widgets)
        if (widget.root && lv_obj_is_valid(widget.root) &&
            lv_obj_get_user_data(widget.root) == widget.state && widget.refresh)
        {
            if (widget.next_refresh_ms != 0 && static_cast<int32_t>(now - widget.next_refresh_ms) < 0) continue;
            widget.next_refresh_ms = now + widget.period_ms;
            widget.refresh(widget.state);
        }
}
void clear_widgets()
{
    auto* state = dashboard_state();
    if (!state) return;
    state->widgets = {};
    lv_obj_clean(state->grid); // Root DELETE callbacks release private PSRAM.
}
WidgetHandle create_widget(WidgetKind kind, lv_obj_t* parent, lv_coord_t width, lv_coord_t height)
{
    switch (kind)
    {
    case WidgetKind::Mesh:
        return create_mesh_widget(parent, width, height);
    case WidgetKind::Gps:
        return create_gps_widget(parent, width, height);
    case WidgetKind::Recent:
        return create_recent_widget(parent, width, height);
    case WidgetKind::Compass:
        return create_compass_widget(parent, width, height);
    case WidgetKind::Agenda:
        return create_agenda_widget(parent, width, height, s_agenda_source);
    case WidgetKind::Music:
        return create_music_widget(parent, width, height, s_music_source ? s_music_source : platform::ui::music_service());
    case WidgetKind::Controls:
        return create_controls_widget(parent, width, height, s_controls_source ? s_controls_source : platform::ui::quick_controls_source());
    default:
        return {};
    }
}
void build_page()
{
    auto* state = dashboard_state();
    if (!state) return;
    clear_widgets();
    lv_obj_update_layout(state->grid);
    const auto width = lv_obj_get_content_width(state->grid);
    const auto height = lv_obj_get_content_height(state->grid);
    if (state->dock_right && state->page == 0)
    {
        const lv_coord_t card_w = (width - 12) / 2;
        const lv_coord_t card_h = (height - 12) / 2;
        const auto count = overview_count();
        for (unsigned index = 0; index < count; ++index)
        {
            const auto w = count == 3 && index == 2 ? width : card_w;
            state->widgets[index] = create_widget(widget_at(index), state->grid, w, card_h);
        }
    }
    else
    {
        const auto kind = widget_at(state->dock_right ? overview_count() + state->page - 1U : state->page);
        state->widgets[0] = create_widget(kind, state->grid, width, height);
    }
    char page[24];
    std::snprintf(page, sizeof(page), "%u / %u", state->page + 1U,
                  page_count(*state));
    lv_label_set_text(state->pager, page);
    // A one-second parent timer would silently override Music/Controls' own
    // shorter periods. Only currently constructed, visible widgets set it.
    uint32_t period = kDashboardTimerMs;
    for (const auto& widget : state->widgets)
        if (widget.root && widget.refresh) period = std::min(period, widget.period_ms);
    if (state->timer) lv_timer_set_period(state->timer, period);
    refresh_dashboard(nullptr);
}
bool change_page(int delta)
{
    auto* state = dashboard_state();
    if (!state) return false;
    const int last = static_cast<int>(page_count(*state)) - 1;
    const int next = std::clamp(static_cast<int>(state->page) + delta, 0, last);
    if (next == state->page) return false;
    state->page = static_cast<uint8_t>(next);
    build_page();
    return true;
}
void feed_offset(void* context, int32_t offset)
{
    auto* state = static_cast<DashboardUi*>(context);
    state->slide_offset = offset;
    lv_obj_set_style_translate_x(state->panel, offset - lv_obj_get_width(state->panel), 0);
    lv_obj_set_style_translate_x(state->menu_grid, offset, 0);
}
void page_offset(void* context, int32_t offset)
{
    auto* state = static_cast<DashboardUi*>(context);
    state->slide_offset = offset;
    if (state->dock_right) lv_obj_set_style_translate_x(state->grid, offset, 0);
    else lv_obj_set_style_translate_y(state->grid, offset, 0);
}
void slide_finished(lv_anim_t*)
{
    auto* state = dashboard_state();
    if (!state) return;
    if (state->slide == SlideKind::Feed)
    {
        // A native scroll/focus animation can otherwise retain an independent
        // offset after our translation reaches its endpoint. The menu owner
        // never scrolls; only the launcher grid owns vertical scrolling.
        lv_obj_scroll_to(lv_obj_get_parent(state->panel), 0, 0, LV_ANIM_OFF);
        state->feed_open = state->feed_target;
#if defined(ARDUINO_T_LORA_PAGER)
        ui::menu_layout::set_bottom_bar_help_text("H Help");
#endif
        feed_offset(state, state->feed_open ? lv_obj_get_width(state->panel) : 0);
        // Keep launcher focus at its normal coordinates once the opaque feed
        // covers it. Otherwise restoring focus after Help scrolls the parent
        // towards the translated, off-screen launcher and displaces the feed.
        lv_obj_set_style_translate_x(state->menu_grid, 0, 0);
        if (!state->feed_open)
        {
            clear_widgets();
            lv_obj_add_flag(state->panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else page_offset(state, 0);
    state->slide = SlideKind::None;
    state->settling = false;
    if (state->timer && state->active && (state->dock_right || state->feed_open)) lv_timer_resume(state->timer);
}
void animate_slide(lv_anim_exec_xcb_t execute, int32_t to, uint32_t duration, lv_anim_completed_cb_t completed)
{
    auto* state = dashboard_state();
    if (!state) return;
    platform::ui::PsramUiLease allocation_scope;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, state);
    lv_anim_set_exec_cb(&animation, execute);
    lv_anim_set_values(&animation, state->slide_offset, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, completed);
    state->settling = true;
    if (!lv_anim_start(&animation))
    {
        execute(state, to);
        completed(nullptr);
    }
}
void prepare_feed()
{
    auto* state = dashboard_state();
    lv_obj_scroll_to(lv_obj_get_parent(state->panel), 0, 0, LV_ANIM_OFF);
    state->slide = SlideKind::Feed;
    if (state->timer) lv_timer_pause(state->timer);
    feed_offset(state, state->feed_open ? lv_obj_get_width(state->panel) : 0);
    lv_obj_remove_flag(state->panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(state->panel);
    // A view exists only once it is being revealed. There is no hidden-page
    // cache and no framebuffer/snapshot allocation for this transition.
    if (!state->feed_open) build_page();
}
void set_feed_open(bool open)
{
    auto* state = dashboard_state();
    if (!state || state->dock_right || state->settling ||
        (state->feed_open == open && state->slide != SlideKind::Feed)) return;
    if (state->slide != SlideKind::Feed) prepare_feed();
    state->feed_target = open;
    animate_slide(feed_offset, open ? lv_obj_get_width(state->panel) : 0, 180, slide_finished);
}
void page_out_finished(lv_anim_t*)
{
    auto* state = dashboard_state();
    if (!state) return;
    const int32_t incoming = -state->slide_offset;
    // Release the outgoing hardware lease before constructing the incoming
    // widget. Both pages never hold services or private state concurrently.
    change_page(state->page_delta);
    page_offset(state, incoming);
    animate_slide(page_offset, 0, 100, slide_finished);
}
void settle_page(int delta)
{
    auto* state = dashboard_state();
    const int last = static_cast<int>(page_count(*state)) - 1;
    const int next = std::clamp(static_cast<int>(state->page) + delta, 0, last);
    if (!delta || next == state->page)
    {
        animate_slide(page_offset, 0, 140, slide_finished);
        return;
    }
    state->page_delta = delta;
    const auto extent = state->dock_right ? lv_obj_get_width(state->grid) : lv_obj_get_height(state->grid);
    animate_slide(page_offset, delta > 0 ? -extent : extent, 80, page_out_finished);
}
void pager_button(lv_event_t* event)
{
    const auto delta = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (delta == 0) set_feed_open(false);
    else change_page(static_cast<int>(delta));
}
void add_pager_button(lv_obj_t* parent, const char* text, lv_align_t align, lv_coord_t x, intptr_t delta)
{
    auto* button = lv_button_create(parent);
    lv_group_remove_obj(button); // Encoder focus on the menu belongs to app icons.
    lv_obj_set_size(button, delta == 0 ? 68 : 38, kPagerHeight - 2);
    lv_obj_align(button, align, x, 0);
    lv_obj_set_style_bg_color(button, ui::theme::surface_alt(), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    auto* label = lv_label_create(button);
    ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_color(label, ui::theme::text(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, pager_button, LV_EVENT_CLICKED, reinterpret_cast<void*>(delta));
}
bool belongs_to(lv_obj_t* object, lv_obj_t* parent)
{
    for (auto* current = object; current; current = lv_obj_get_parent(current))
        if (current == parent) return true;
    return false;
}
void update_menu_pointer(lv_indev_t* input, lv_event_code_t code);
void track_pointer(lv_timer_t*)
{
    auto* state = dashboard_state();
    if (state && state->pressed)
        update_menu_pointer(state->pointer, lv_indev_get_state(state->pointer) == LV_INDEV_STATE_PRESSED ? LV_EVENT_PRESSING : LV_EVENT_RELEASED);
}
void stop_pointer_tracking()
{
    auto* state = dashboard_state();
    if (!state) return;
    state->pressed = false;
    if (state->pointer_timer) lv_timer_delete(state->pointer_timer);
    state->pointer_timer = nullptr;
}
void update_menu_pointer(lv_indev_t* input, lv_event_code_t code)
{
    auto* state = dashboard_state();
    if (!state || !state->active || !input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return;
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    if (ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::Menu ||
        ui_is_overlay_active() || !lv_obj_is_visible(ui::menu_layout::menuPanel()) ||
        (lv_group_get_default() && lv_group_get_default() != menu_g))
    {
        stop_pointer_tracking();
        if (!state->settling)
        {
            if (state->slide == SlideKind::Feed) set_feed_open(state->feed_open);
            else if (state->slide == SlideKind::Page) settle_page(0);
        }
        return;
    }
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED)
    {
        if (state->pressed && state->pointer == input) return;
        stop_pointer_tracking();
        if (state->settling)
        {
            lv_indev_wait_release(input);
            lv_indev_stop_processing(input);
            return;
        }
        // A slider owns its complete drag, even when a page threshold is crossed.
        for (auto* object = lv_indev_get_active_obj(); object; object = lv_obj_get_parent(object))
            if (lv_obj_check_type(object, &lv_slider_class)) return;
        lv_area_t area;
        lv_obj_get_coords(ui::menu_layout::menuPanel(), &area);
        if (point.x < area.x1 || point.x > area.x2 || point.y < area.y1 || point.y > area.y2) return;
        lv_obj_get_coords(state->panel, &area);
        state->inside_dashboard = point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2;
        state->pointer = input;
        state->start = point;
        state->pressed = true;
        state->slide = SlideKind::None;
        // LVGL 9 does not send PRESSING to indev listeners. Poll the already
        // sampled pointer only for the lifetime of this press, on the UI task.
        platform::ui::PsramUiLease allocation_scope;
        state->pointer_timer = lv_timer_create(track_pointer, 16, nullptr);
        return;
    }
    if (!state->pressed || state->pointer != input) return;
    auto* target = lv_indev_get_active_obj();
    if (target && !belongs_to(target, ui::menu_layout::menuPanel()) && state->slide == SlideKind::None)
    {
        stop_pointer_tracking(); // A modal or another screen owns this press.
        return;
    }
    const int dx = point.x - state->start.x;
    const int dy = point.y - state->start.y;
    const int threshold = std::clamp<lv_coord_t>(lv_obj_get_width(state->panel) / 7, 36, 96);
    const bool horizontal = std::abs(dx) >= 12 && std::abs(dx) > std::abs(dy) + 8;
    const bool vertical = std::abs(dy) >= 12 && std::abs(dy) > std::abs(dx) + 8;
    if (state->slide == SlideKind::None)
    {
        if (state->dock_right && horizontal)
            state->slide = state->inside_dashboard ? SlideKind::Page : SlideKind::Ignore;
        else if (!state->dock_right && horizontal && ((dx > 0 && !state->feed_open) || (dx < 0 && state->feed_open))) prepare_feed();
        else if (!state->dock_right && state->feed_open && vertical) state->slide = SlideKind::Page;
        if (state->slide != SlideKind::None && state->timer) lv_timer_pause(state->timer);
    }
    if (state->slide != SlideKind::None)
    {
        // Keep receiving pointer motion while suppressing the original control's
        // click/long-press. A drag is committed only on release, so reversing or
        // stopping below the threshold naturally returns to the original page.
        lv_indev_stop_processing(input);
        if (state->slide == SlideKind::Feed)
        {
            const auto width = lv_obj_get_width(state->panel);
            feed_offset(state, std::clamp<int32_t>((state->feed_open ? width : 0) + dx, 0, width));
        }
        else if (state->slide == SlideKind::Page)
        {
            const int extent = state->dock_right ? lv_obj_get_width(state->grid) : lv_obj_get_height(state->grid);
            const int offset = state->dock_right ? dx : dy;
            page_offset(state, std::clamp(offset, -extent, extent));
        }
        if (code == LV_EVENT_RELEASED)
        {
            stop_pointer_tracking();
            lv_indev_reset(input, nullptr);
            lv_indev_wait_release(input);
            if (state->slide == SlideKind::Feed)
                set_feed_open(state->feed_open ? dx > -threshold : dx >= threshold);
            else if (state->slide == SlideKind::Page)
            {
                const int distance = state->dock_right ? dx : dy;
                settle_page(std::abs(distance) >= (state->dock_right ? threshold : 36) ? (distance < 0 ? 1 : -1) : 0);
            }
            else slide_finished(nullptr);
        }
        return;
    }
    if (code == LV_EVENT_RELEASED) stop_pointer_tracking();
}
void menu_pointer(lv_event_t* event)
{
    update_menu_pointer(static_cast<lv_indev_t*>(lv_event_get_current_target(event)), lv_event_get_code(event));
}
void panel_deleted(lv_event_t* event)
{
    auto* state = dashboard_state();
    if (!state || lv_event_get_target(event) != state->panel) return;
    if (state->menu_grid && lv_obj_is_valid(state->menu_grid))
        lv_obj_set_style_translate_x(state->menu_grid, 0, 0);
    if (state->feed_open && state->menu_grid && lv_obj_is_valid(state->menu_grid))
        lv_obj_remove_flag(state->menu_grid, LV_OBJ_FLAG_HIDDEN);
    dashboard_reset();
}
} // namespace

void bindAgendaSource(ui::agenda::IAgendaSource* source) { s_agenda_source = source; }
void bindControlsSource(ui::controls::IQuickControls* source) { s_controls_source = source; }
void bindMusicSource(::music::IService* source) { s_music_source = source; }

bool isFeedOpen()
{
    auto* state = dashboard_state();
    return state && state->active && state->feed_open;
}
bool handleEncoder(int delta)
{
    if (!isFeedOpen() || delta == 0 || ui_is_overlay_active() ||
        ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::Menu ||
        (lv_group_get_default() && lv_group_get_default() != menu_g)) return false;
    change_page(delta > 0 ? 1 : -1);
    return true;
}
bool handleKey(uint32_t key, bool at_left_edge)
{
    auto* state = dashboard_state();
    if (!state || !state->active || state->dock_right ||
        ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::Menu || ui_is_overlay_active() ||
        (lv_group_get_default() && lv_group_get_default() != menu_g)) return false;
#if defined(ARDUINO_T_LORA_PAGER)
    if (key == 'v' || key == 'V')
    {
        set_feed_open(!state->feed_open);
        return true;
    }
#endif
    if (state->feed_open)
    {
        for (auto& widget : state->widgets)
            if (widget.key && widget.key(widget.state, key)) return true;
        if (key == 'w' || key == 'W' || key == LV_KEY_UP || key == LV_KEY_PREV) change_page(-1);
        else if (key == 's' || key == 'S' || key == LV_KEY_DOWN || key == LV_KEY_NEXT) change_page(1);
        else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) set_feed_open(false);
#if !defined(ARDUINO_T_LORA_PAGER)
        else if (key == 'd' || key == 'D' || key == LV_KEY_RIGHT) set_feed_open(false);
#endif
        else if (key == 'd' || key == 'D' || key == LV_KEY_RIGHT) return true;
        else if (key != LV_KEY_ENTER && key != 'a' && key != 'A' && key != LV_KEY_LEFT) return false;
        return true;
    }
#if !defined(ARDUINO_T_LORA_PAGER)
    if (at_left_edge && (key == LV_KEY_LEFT || key == 'a' || key == 'A'))
    {
        set_feed_open(true);
        return true;
    }
#else
    (void)at_left_edge;
#endif
    return false;
}

bool handleEncoderButton(bool pressed)
{
#if defined(ARDUINO_T_LORA_PAGER)
    auto* state = dashboard_state();
    if (!state) return false;
    if (state->encoder_button_owned)
    {
        if (!pressed) state->encoder_button_owned = false;
        return true;
    }
    if (!pressed || !isFeedOpen() || ui_is_overlay_active() ||
        (lv_group_get_default() && lv_group_get_default() != menu_g)) return false;
    // Own the whole press/release pair, even if activation opens an app and
    // releases the widget. A held switch must never fire on the new page.
    state->encoder_button_owned = true;
    handleKey(LV_KEY_ENTER);
    return true;
#else
    (void)pressed;
    return false;
#endif
}

void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions&)
{
    if (!dashboard_enabled() || !menu_panel || !grid_panel) return;
    if (auto* previous = dashboard_state())
    {
        auto* panel = previous->panel;
        if (previous->feed_open && lv_obj_is_valid(previous->menu_grid))
            lv_obj_remove_flag(previous->menu_grid, LV_OBJ_FLAG_HIDDEN);
        dashboard_reset();
        if (panel && lv_obj_is_valid(panel)) lv_obj_delete(panel);
    }
    if (!dashboard_create()) return; // PSRAM exhaustion never falls back to RAM.
    platform::ui::PsramUiLease allocation_scope;
    auto& state = *dashboard_state();
    state.menu_grid = grid_panel;
    lv_obj_set_style_translate_x(grid_panel, 0, 0);
    // Only the launcher grid scrolls; inherited tile scrolling would move the
    // whole dashboard horizontally while the finger drags the feed into view.
    lv_obj_remove_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(menu_panel, LV_DIR_NONE);
    lv_obj_scroll_to(menu_panel, 0, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(grid_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_remove_flag(grid_panel, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_update_layout(menu_panel);
    const auto& profile = ui::menu_profile::current();
    const auto screen_w = lv_obj_get_content_width(menu_panel);
    const auto screen_h = lv_obj_get_content_height(menu_panel);
    state.dock_right = std::strcmp(profile.name, "t_display_p4") == 0;
    const lv_coord_t width = state.dock_right ? screen_w - lv_obj_get_width(grid_panel) - 16 : screen_w;
    const lv_coord_t height = state.dock_right ? screen_h - profile.grid_top_offset - profile.top_bar_height - 8 : screen_h - profile.top_bar_height;
    state.panel = lv_obj_create(menu_panel);
    lv_obj_set_size(state.panel, width, height);
    lv_obj_align(state.panel, state.dock_right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, state.dock_right ? -8 : 0, state.dock_right ? profile.grid_top_offset : 0);
    lv_obj_set_style_bg_color(state.panel, ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(state.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.panel, 0, 0);
    lv_obj_set_style_pad_all(state.panel, 0, 0);
    lv_obj_clear_flag(state.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(state.panel, panel_deleted, LV_EVENT_DELETE, nullptr);
    state.grid = lv_obj_create(state.panel);
    lv_obj_set_size(state.grid, width, height - kPagerHeight);
    lv_obj_set_style_bg_opa(state.grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state.grid, 0, 0);
    lv_obj_set_style_pad_all(state.grid, 0, 0);
    lv_obj_set_style_pad_row(state.grid, 12, 0);
    lv_obj_set_style_pad_column(state.grid, 12, 0);
    lv_obj_set_flex_flow(state.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(state.grid, LV_OBJ_FLAG_SCROLLABLE);
    state.pager = lv_label_create(state.panel);
    lv_obj_align(state.pager, LV_ALIGN_BOTTOM_MID, 0, -3);
    if (!state.dock_right) add_pager_button(state.panel, "Menu", LV_ALIGN_BOTTOM_LEFT, 0, 0);
    add_pager_button(state.panel, state.dock_right ? LV_SYMBOL_LEFT : LV_SYMBOL_UP, LV_ALIGN_BOTTOM_RIGHT, -42, -1);
    add_pager_button(state.panel, state.dock_right ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN, LV_ALIGN_BOTTOM_RIGHT, 0, 1);
    state.timer = lv_timer_create(refresh_dashboard, kDashboardTimerMs, nullptr);
    if (state.dock_right) build_page();
    else
    {
        lv_obj_add_flag(state.panel, LV_OBJ_FLAG_HIDDEN);
        if (state.timer) lv_timer_pause(state.timer);
    }
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
    {
        if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) continue;
        lv_indev_remove_event_cb_with_user_data(input, menu_pointer, nullptr);
        lv_indev_add_event_cb(input, menu_pointer, LV_EVENT_ALL, nullptr);
    }
}
void bringToFront()
{
    if (auto* state = dashboard_state()) lv_obj_move_foreground(state->panel);
}
void refresh_localized_text()
{
    if (auto* state = dashboard_state(); state && state->active && lv_obj_is_visible(state->panel)) build_page();
}
void setActive(bool active)
{
    auto* state = dashboard_state();
    if (!state || state->active == active) return;
    state->active = active;
    stop_pointer_tracking();
    if (!active)
    {
        lv_anim_delete(state, nullptr);
        state->settling = false;
        state->slide = SlideKind::None;
        state->feed_open = false;
        lv_obj_set_style_translate_x(state->menu_grid, 0, 0);
        page_offset(state, 0);
        clear_widgets();
        if (!state->dock_right) lv_obj_add_flag(state->panel, LV_OBJ_FLAG_HIDDEN);
        if (state->timer) lv_timer_pause(state->timer);
    }
    else if (state->dock_right)
    {
        build_page();
        if (state->timer) lv_timer_resume(state->timer);
    }
#if defined(ARDUINO_T_LORA_PAGER)
    ui::menu_layout::set_bottom_bar_help_text("H Help");
#endif
}
} // namespace ui::menu::dashboard
#endif
