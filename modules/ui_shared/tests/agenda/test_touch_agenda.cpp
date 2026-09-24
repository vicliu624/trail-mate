#include "popup_test_support.h"
#include "product_composition/agenda_composition.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_page_flow.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_lvgl_ux_packs/common/input_layout.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include "ui_presentation/waypoint/waypoint_model.h"

#include <cassert>
#include <cstring>
#include <string>

namespace ui::i18n
{
extern bool test_pinyin_enabled;
}

namespace
{
namespace page = ui::agenda::page;
class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord record{};
    unsigned writes = 0;
    agenda::StoreResult readSlot(uint16_t, agenda::EventRecord& out) override
    {
        out = record;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t, const agenda::EventRecord& value) override
    {
        record = value;
        ++writes;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 1; }
};
class Clock final : public agenda::IAgendaClock
{
    agenda::ClockSample sample() const override { return {1776330000, 0, 0, true}; }
};
class WaypointStore final : public waypoint::IStore
{
  public:
    waypoint::Record record{};
    unsigned writes = 0;
    uint16_t slotCount() const override { return 1; }
    waypoint::Result read(uint16_t, waypoint::Record& out) override
    {
        out = record;
        return waypoint::Result::Ok;
    }
    waypoint::Result create(const waypoint::Record& value, uint32_t& id) override
    {
        record = value;
        record.id = id = 1;
        ++writes;
        return waypoint::Result::Ok;
    }
    waypoint::Result update(const waypoint::Record&) override { return waypoint::Result::IoError; }
    waypoint::Result remove(uint32_t) override { return waypoint::Result::IoError; }
};
lv_obj_t* find(lv_obj_t* object, const lv_obj_class_t* type, const char* text = nullptr)
{
    if (lv_obj_check_type(object, type) && (!text || std::strcmp(lv_label_get_text(object), text) == 0)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = find(lv_obj_get_child(object, i), type, text)) return found;
    return nullptr;
}
void click(lv_obj_t* root, const char* text)
{
    auto* label = find(root, &lv_label_class, text);
    assert(label && lv_obj_check_type(lv_obj_get_parent(label), &lv_button_class));
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
}
void key(const char* text)
{
    auto* matrix = find(lv_layer_top(), &lv_buttonmatrix_class);
    assert(matrix);
    const char* const* map = lv_buttonmatrix_get_map(matrix);
    uint32_t button = 0;
    for (unsigned i = 0; map[i][0]; ++i)
    {
        if (std::strcmp(map[i], "\n") == 0) continue;
        if (std::strcmp(map[i], text) == 0)
        {
            lv_obj_send_event(matrix, LV_EVENT_VALUE_CHANGED, &button);
            return;
        }
        ++button;
    }
    assert(false && "Keyboard key missing");
}
void pump()
{
    lv_tick_inc(60);
    lv_timer_handler();
}
void open(lv_obj_t* source)
{
    lv_obj_send_event(source, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(lv_layer_top());
    auto* matrix = find(lv_layer_top(), &lv_buttonmatrix_class);
    assert(matrix && find(lv_layer_top(), &lv_textarea_class));
    lv_area_t area;
    lv_obj_get_coords(matrix, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 320 && area.y2 < 240);
}
} // namespace

void testTouchAgenda()
{
    ui_lvgl_ux::DeviceUxProfile ux;
    ux.screen_class = ui_lvgl_ux::ScreenClass::DeckLandscape;
    ux.input_model = ui_lvgl_ux::InputModel::Touch;
    ui_lvgl_ux::configureInputLayout(ux);
    assert(ui::page_profile::current().compact_touch_keyboard);
    auto* display = lv_display_create(320, 240);
    Store store;
    Clock clock;
    product_composition::AgendaComposition composition(store, clock);
    composition.setStorageReady(true);
    ui::workspace::PresentationWorkspace workspace;
    composition.bind(workspace);
    page::Host host;
    host.source = workspace.agenda;
    host.actions = workspace.agenda;
    WaypointStore waypoint_store;
    ui::waypoint::Model waypoints(waypoint_store);
    waypoints.setReady(true);
    host.waypoints = &waypoints;
    host.waypoint_actions = &waypoints;
    page::Flow flow(host);
    page::Flow::enter(&flow, lv_screen_active());
    click(page::state()->root, "New");
    pump();
    auto* title = page::state()->editor_widgets.title;
    open(title);
    key("r");
    key("a");
    key("d");
    key("i");
    key("o");
    assert(std::strcmp(lv_textarea_get_text(title), "") == 0);
    assert(std::strcmp(lv_textarea_get_text(find(lv_layer_top(), &lv_textarea_class)), "radio") == 0);
    agenda_test_ui::interruption = true;
    page::Flow::exit(&flow, lv_screen_active());
    assert(!page::state() && lv_obj_get_child_count(lv_layer_top()) == 0);
    agenda_test_ui::interruption = false;
    page::Flow::enter(&flow, lv_screen_active());
    flow.tick();
    title = page::state()->editor_widgets.title;
    auto* resumed_text = find(lv_layer_top(), &lv_textarea_class);
    assert(resumed_text && std::strcmp(lv_textarea_get_text(resumed_text), "radio") == 0);
    assert(std::strcmp(lv_textarea_get_text(title), "") == 0);
    click(lv_layer_top(), "Cancel");
    assert(lv_obj_get_child_count(lv_layer_top()) == 0 && store.writes == 0);
    assert(std::strcmp(lv_textarea_get_text(title), "") == 0);

    open(title);
    key("r");
    key("a");
    key("d");
    key("i");
    key("o");
    click(lv_layer_top(), "OK");
    assert(std::strcmp(lv_textarea_get_text(title), "radio") == 0 && store.writes == 0);
    open(page::state()->editor_widgets.note);
    key("o");
    key("k");
    click(lv_layer_top(), "OK");
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.note), "ok") == 0);
    click(page::state()->root, "Save");
    pump();
    assert(page::state()->awaiting_command);
    composition.tick(false);
    pump();
    assert(page::state()->view == page::View::Agenda && store.writes == 1);
    assert(std::strcmp(store.record.title, "radio") == 0 && std::strcmp(store.record.note, "ok") == 0);
    assert((store.record.flags & agenda::HasNote) && !(store.record.flags & agenda::HasLocation));
    click(page::state()->root, "New");
    pump();
    const auto interrupt = [&]()
    {
        agenda_test_ui::interruption = true;
        page::Flow::exit(&flow, lv_screen_active());
        assert(!page::state() && lv_obj_get_child_count(lv_layer_top()) == 0);
        agenda_test_ui::interruption = false;
        page::Flow::enter(&flow, lv_screen_active());
        flow.tick();
        assert(page::state() && find(lv_layer_top(), &lv_textarea_class));
    };
    open(page::state()->editor_widgets.note);
    click(lv_layer_top(), "EN"); // No script enabled: cycles to numeric mode.
    key("Shift");
    key("[");
    auto* modal = find(lv_layer_top(), &lv_textarea_class);
    lv_textarea_set_cursor_pos(modal, 0);
    char captured[320]{};
    ui::widgets::ImeEditState editing;
    char guard[] = {'x', 'y'};
    assert(ui::widgets::capture_touch_text_editor(page::state()->editor_widgets.note, guard, 1, editing) ==
           ui::widgets::TouchEditorCapture::InsufficientCapacity);
    assert(guard[0] == 'x' && guard[1] == 'y');
    interrupt();
    assert(ui::widgets::capture_touch_text_editor(page::state()->editor_widgets.note, captured, sizeof(captured), editing) ==
           ui::widgets::TouchEditorCapture::Captured);
    assert(std::strcmp(captured, "[") == 0 && editing.shift && editing.cursor == 0);
    assert(editing.mode == static_cast<uint8_t>(ui::widgets::ImeWidget::Mode::NUM));
    key("]"); // Restored symbol keyboard, not its default alphabetic layout.
    click(lv_layer_top(), "OK");
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.note), "[]") == 0);

    ui::i18n::test_pinyin_enabled = true;
    open(page::state()->editor_widgets.title);
    click(lv_layer_top(), "EN");
    key("n");
    key("i");
    assert(find(lv_layer_top(), &lv_label_class, "\xE4\xBD\xA0"));
    interrupt();
    assert(ui::widgets::capture_touch_text_editor(page::state()->editor_widgets.title, captured, sizeof(captured), editing) ==
           ui::widgets::TouchEditorCapture::Captured);
    assert(captured[0] == '\0' && std::strcmp(editing.composition, "ni") == 0);
    click(lv_layer_top(), "\xE6\xB3\xA5");
    click(lv_layer_top(), "OK");
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "\xE6\xB3\xA5") == 0);
    assert(store.writes == 1); // Neither recovery nor OK saves the event.
    ui::i18n::test_pinyin_enabled = false;
    std::string long_title, original_note, modal_note;
    for (unsigned i = 0; i < 39; ++i) long_title += "\xF0\x9F\x8F\x95";
    for (unsigned i = 0; i < 79; ++i)
    {
        original_note += "\xF0\x9F\x8F\x95";
        modal_note += "\xF0\x9F\x8C\xB2";
    }
    lv_textarea_set_text(page::state()->editor_widgets.title, long_title.c_str());
    lv_textarea_set_text(page::state()->editor_widgets.note, original_note.c_str());
    open(page::state()->editor_widgets.note);
    lv_textarea_set_text(find(lv_layer_top(), &lv_textarea_class), modal_note.c_str());
    interrupt();
    assert(modal_note == lv_textarea_get_text(find(lv_layer_top(), &lv_textarea_class)));
    assert(original_note == lv_textarea_get_text(page::state()->editor_widgets.note));
    assert(long_title == lv_textarea_get_text(page::state()->editor_widgets.title));
    click(lv_layer_top(), "Cancel");
    assert(original_note == lv_textarea_get_text(page::state()->editor_widgets.note));
    // Name a location through the production picker and compact touch editor.
    // Modal OK updates the field only; only Save may enqueue persistence.
    lv_textarea_set_text(page::state()->editor_widgets.title, "Walk");
    lv_textarea_set_text(page::state()->editor_widgets.note, "");
    page::state()->draft.event.flags |= agenda::HasLocation;
    const auto action = [](page::Action value)
    {
        page::runtime::request(value);
        pump();
    };
    action(page::Action::PickLocation);
    action(page::Action::PickWaypoint);
    action(page::Action::NameWaypoint);
    assert(page::state()->view == page::View::WaypointName);
    lv_textarea_set_text(page::state()->editor_widgets.title, "Camp");
    open(page::state()->editor_widgets.title);
    lv_textarea_set_cursor_pos(find(lv_layer_top(), &lv_textarea_class), LV_TEXTAREA_CURSOR_LAST);
    key("x");
    interrupt();
    assert(!std::strcmp(lv_textarea_get_text(find(lv_layer_top(), &lv_textarea_class)), "Campx"));
    assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Camp"));
    click(lv_layer_top(), "Cancel");
    assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Camp"));
    assert(waypoint_store.writes == 0 && store.writes == 1);
    open(page::state()->editor_widgets.title);
    lv_textarea_set_cursor_pos(find(lv_layer_top(), &lv_textarea_class), LV_TEXTAREA_CURSOR_LAST);
    key("x");
    interrupt();
    click(lv_layer_top(), "OK");
    assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Campx"));
    assert(waypoint_store.writes == 0 && store.writes == 1);
    click(page::state()->root, "Save");
    pump();
    assert(page::state()->awaiting_command && waypoint_store.writes == 0);
    waypoints.pump();
    pump();
    assert(page::state()->view == page::View::Editor);
    assert(waypoint_store.writes == 1 && store.writes == 1);
    assert(!std::strcmp(waypoint_store.record.name, "Campx"));
    assert(!std::strcmp(page::state()->draft.event.location_name, "Campx"));
    page::Flow::exit(&flow, lv_screen_active());
    assert(!page::state() && lv_obj_get_child_count(lv_layer_top()) == 0);
    ui::page_profile::set_active_profile(nullptr);
    lv_display_delete(display);
}
