#include "popup_test_support.h"
#include "product_composition/agenda_composition.h"
#include "ui/app_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_page_flow.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/gps/gps_page_runtime.h"
#include "ui_presentation/map/map_location_request.h"
#include "ui_presentation/map/map_target_request.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace
{
namespace page = ui::agenda::page;
ui::map::MapLocationRequest* request = nullptr;
ui::map::MapTargetRequest* target = nullptr;
const ui::page::Host* navigation = nullptr;
lv_obj_t* map_tree = nullptr;
lv_group_t* map_group = nullptr;
bool available = true;
bool fail_enter = false;
unsigned enters = 0;
unsigned exits = 0;
class Store final : public agenda::IAgendaStore
{
  public:
    unsigned writes = 0;
    agenda::EventRecord records[2]{};
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        out = records[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t, const agenda::EventRecord&) override
    {
        ++writes;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 2; }
};
class Clock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample sample() const override { return {1776330000, 0, 0, true}; }
};
void action(page::Action value)
{
    page::runtime::request(value);
    lv_tick_inc(60);
    lv_timer_handler();
}
bool hasLabel(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (hasLabel(lv_obj_get_child(root, i), text)) return true;
    return false;
}
} // namespace

// Substitute only the Map shell boundary: the real Agenda coordinator, LVGL
// tree, input groups, draft capture and presentation model run unchanged.
// This proves ownership/return semantics, not tile rendering or device input.
namespace gps::ui::runtime
{
bool is_available() { return available; }
} // namespace gps::ui::runtime
namespace gps::ui::shell
{
void enter_route(const RouteSpec* route, lv_obj_t* parent)
{
    assert(!page::state()); // Full editor tree and State have already gone.
    assert(lv_obj_get_child_count(parent) == 0);
    assert(!map_tree && !request && !target && route->projection == Projection::Map);
    ++enters;
    request = route->location;
    target = route->target;
    assert((request != nullptr) != (target != nullptr));
    navigation = route->host;
    if (fail_enter) return;
    if (request) request->result.state = ::ui::map::MapLocationSelectionState::Selecting;
    if (target) target->entered = true;
    map_tree = lv_obj_create(parent);
    map_group = lv_group_create();
    set_default_group(map_group);
    ui_set_overlay_active(true);
}
void exit_route(const RouteSpec*, lv_obj_t*)
{
    ++exits;
    if (fail_enter && request) request->result.state = ::ui::map::MapLocationSelectionState::Cancelled;
    if (map_tree) lv_obj_delete(map_tree);
    if (map_group)
    {
        set_default_group(nullptr);
        lv_group_delete(map_group);
    }
    map_tree = nullptr;
    map_group = nullptr;
    request = nullptr;
    if (target) target->entered = false;
    target = nullptr;
    navigation = nullptr;
    ui_set_overlay_active(false);
}
} // namespace gps::ui::shell

void cancelTestMap()
{
    assert(request && navigation && !page::state());
    request->result.state = ui::map::MapLocationSelectionState::Cancelled;
    ui::page::request_exit(navigation);
}
void assertTestMapTarget(double latitude, double longitude)
{
    assert(target && target->entered && !request && !page::state() && map_tree);
    assert(target->viewport.center_lat == latitude && target->viewport.center_lon == longitude);
}
void returnFromTestMapTarget()
{
    assert(target && navigation && !page::state());
    ui::page::request_exit(navigation);
}

void testMapFlow()
{
    for (const bool wide : {false, true})
    {
        auto profile = wide ? ui::page_profile::make_pager_profile() : ui::page_profile::make_tdeck_profile();
        ui::page_profile::set_active_profile(&profile);
        auto* display = lv_display_create(wide ? 480 : 320, wide ? 222 : 240);
        auto* parent = lv_screen_active();
        Store store;
        Clock clock;
        product_composition::AgendaComposition composition(store, clock);
        composition.setStorageReady(true);
        ui::workspace::PresentationWorkspace workspace;
        composition.bind(workspace);
        page::Host host;
        host.source = workspace.agenda;
        host.actions = workspace.agenda;
        page::Flow flow(host);
        page::Flow::enter(&flow, parent);
        action(page::Action::NewEvent);
        lv_textarea_set_text(page::state()->editor_widgets.title, "Unsaved campsite");
        lv_textarea_set_text(page::state()->editor_widgets.note, "Keep this draft");
        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        assert(flow.mapActive() && page::state()); // Request is deferred.
        agenda_test_ui::interruption = true;
        flow.tick();
        assert(page::state() && !request);
        agenda_test_ui::interruption = false;
        flow.tick();
        assert(!page::state() && request && map_tree);
        assert(!request->has_initial_viewport);
        request->result = {24.8731, 118.0, ui::map::MapLocationSelectionState::Picked};
        ui::page::request_exit(navigation);
        assert(!page::state()); // Callback must not delete the active Map.
        flow.tick();
        assert(!flow.mapActive() && !map_tree && !request);
        assert(page::state()->view == page::View::Editor);
        assert(lv_group_get_default() == page::state()->group);
        const auto draft = page::state()->draft;
        assert(std::strcmp(draft.event.title, "Unsaved campsite") == 0);
        assert(std::strcmp(draft.event.note, "Keep this draft") == 0);
        assert(draft.event.latitude_e7 == 248731000 && draft.event.longitude_e7 == 1180000000);
        assert(draft.dirty && store.writes == 0);

        page::state()->editor_return = page::View::Detail;
        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        flow.tick();
        assert(request->has_initial_viewport && request->initial_viewport.center_lat == 24.8731);
        request->result.state = ui::map::MapLocationSelectionState::Cancelled;
        ui::page::request_exit(navigation);
        flow.tick();
        assert(page::state()->editor_return == page::View::Detail);
        assert(std::memcmp(&page::state()->draft.event, &draft.event, sizeof(draft.event)) == 0);
        assert(store.writes == 0);

        available = false;
        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        assert(!flow.mapActive() && page::state()->error);
        available = true;
        fail_enter = true;
        action(page::Action::ChooseOnMap);
        flow.tick();
        flow.tick();
        assert(page::state() && page::state()->error && !flow.mapActive());
        assert(std::strcmp(page::state()->draft.event.title, "Unsaved campsite") == 0);
        fail_enter = false;

        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        flow.tick();
        assert(map_tree);
        page::Flow::exit(&flow, parent);
        assert(!page::state() && !map_tree && !flow.mapActive());
        assert(lv_obj_get_child_count(parent) == 0 && store.writes == 0);
        // Closing a pending transition must discard only the return context.
        page::Flow::enter(&flow, parent);
        action(page::Action::NewEvent);
        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        page::Flow::exit(&flow, parent);
        assert(!page::state() && !flow.mapActive());
        assert(enters == exits);

        // A non-first recurring occurrence must keep its date when Map has
        // reconstructed the list and unsaved editing is subsequently discarded.
        for (unsigned i = 0; i < 2; ++i)
        {
            auto& record = store.records[i];
            record.state = agenda::RecordState::Active;
            record.id = i + 1;
            record.start_time = clock.sample().calendar_seconds + (i + 1) * 3600;
            std::snprintf(record.title, sizeof(record.title), "Event %u", i + 1);
        }
        auto& recurring = store.records[1];
        recurring.start_time -= 6 * 86400;
        recurring.flags = agenda::HasEndTime;
        recurring.end_time = recurring.start_time + 3600;
        recurring.repeat = agenda::Repeat::Weekly;
        page::Flow::enter(&flow, parent);
        assert(page::state()->snapshot.page.count >= 2);
        const auto occurrence = page::state()->snapshot.page.rows[1].occurrence;
        assert(occurrence.event_id == 2 && occurrence.start != recurring.start_time);
        agenda::CivilTime start, end;
        assert(agenda::fromCalendarSeconds(occurrence.start, start));
        assert(agenda::fromCalendarSeconds(occurrence.end, end));
        char expected[48];
        std::snprintf(expected, sizeof(expected), "%04u-%02u-%02u  %02u:%02u - %02u:%02u",
                      start.year, start.month, start.day, start.hour, start.minute, end.hour, end.minute);
        page::runtime::request(page::Action::OpenRow, 1);
        lv_tick_inc(60);
        lv_timer_handler();
        assert(hasLabel(page::state()->root, expected));
        action(page::Action::EditEvent);
        lv_textarea_set_text(page::state()->editor_widgets.title, "Do not save this title");
        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        flow.tick();
        cancelTestMap();
        flow.tick();
        action(page::Action::Back);
        assert(page::state()->confirm_discard);
        action(page::Action::Discard);
        assert(page::state()->view == page::View::Detail);
        assert(hasLabel(page::state()->root, "Event 2"));
        assert(hasLabel(page::state()->root, expected));
        assert(store.writes == 0);
        assert(!hasLabel(page::state()->root, "Navigate"));
        recurring.flags |= agenda::HasLocation;
        recurring.latitude_e7 = 248731000;
        recurring.longitude_e7 = 1180000000;
        std::strcpy(recurring.location_name, "Campsite");
        page::runtime::request(page::Action::OpenRow, 1);
        lv_tick_inc(60);
        lv_timer_handler();
        assert(hasLabel(page::state()->root, "Navigate"));
        action(page::Action::Navigate);
        assert(page::state() && flow.mapActive());
        flow.tick();
        assert(!page::state() && target && !request && map_tree);
        assert(target->viewport.center_lat == 24.8731 && target->viewport.center_lon == 118.0);
        assert(std::strcmp(target->label.c_str(), "Campsite") == 0);
        ui::page::request_exit(navigation);
        assert(map_tree); // Back callback cannot tear down its own widget tree.
        flow.tick();
        assert(page::state()->view == page::View::Detail && !target && !map_tree);
        assert(hasLabel(page::state()->root, expected));
        assert(page::state()->draft.event.latitude_e7 == recurring.latitude_e7);
        assert(store.writes == 0);
        fail_enter = true;
        action(page::Action::Navigate);
        flow.tick();
        flow.tick();
        assert(page::state()->view == page::View::Detail && page::state()->error && !flow.mapActive());
        assert(hasLabel(page::state()->root, expected) && store.writes == 0);
        fail_enter = false;
        // Reminder navigation can interrupt a picker without accepting its
        // provisional values or substituting the reminder for the draft.
        action(page::Action::EditEvent);
        lv_textarea_set_text(page::state()->editor_widgets.title, "Draft before reminder");
        action(page::Action::PickTime);
        const auto original_start = page::state()->draft.event.start_time;
        const int32_t times[] = {7, 23, 9, 41};
        for (unsigned i = 0; i < 4; ++i) lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[i], times[i]);
        lv_obj_add_state(page::state()->editor_widgets.end_enabled, LV_STATE_CHECKED);
        assert(!flow.prepareDestination(0, 0));
        flow.tick();
        assert(page::state() && !target); // Prepared is not yet committed.
        flow.finishDestination(false);
        assert(!flow.mapActive() && page::state()->view == page::View::TimePicker);
        assert(!flow.prepareDestination(0, 0));
        flow.finishDestination(true);
        agenda_test_ui::transition = true;
        flow.tick();
        assert(page::state() && !target);
        agenda_test_ui::transition = false;
        flow.tick();
        assertTestMapTarget(0, 0);
        returnFromTestMapTarget();
        flow.tick();
        assert(page::state()->view == page::View::TimePicker);
        for (unsigned i = 0; i < 4; ++i) assert(lv_spinbox_get_value(page::state()->editor_widgets.clock_fields[i]) == times[i]);
        assert(lv_obj_has_state(page::state()->editor_widgets.end_enabled, LV_STATE_CHECKED));
        assert(page::state()->draft.event.start_time == original_start && store.writes == 0);
        assert(std::strcmp(page::state()->draft.event.title, "Draft before reminder") == 0);
        action(page::Action::Back);
        action(page::Action::PickDate);
        action(page::Action::NextMonth);
        const auto month = page::state()->picker.date;
        assert(!flow.prepareDestination(10000000, 20000000));
        flow.finishDestination(true);
        flow.tick();
        returnFromTestMapTarget();
        flow.tick();
        assert(page::state()->view == page::View::DatePicker);
        assert(page::state()->picker.date.month == month.month && page::state()->picker.date.year == month.year);
        assert(page::state()->draft.event.start_time == original_start && store.writes == 0);
        // Media loss closes an active Map before releasing its return context.
        assert(!flow.prepareDestination(10000000, 20000000));
        flow.finishDestination(true);
        flow.tick();
        assert(map_tree && target && !page::state());
        composition.setStorageReady(false);
        flow.invalidateStorage();
        assert(!map_tree && !target && !request && !flow.mapActive());
        assert(page::state() && page::state()->view == page::View::Agenda);
        assert(!page::state()->snapshot.storage_ready && page::state()->draft.event.id == 0);
        assert(lv_group_get_default() == page::state()->group);
        composition.setStorageReady(true);
        page::runtime::refresh();
        action(page::Action::NewEvent);
        lv_textarea_set_text(page::state()->editor_widgets.title, "Old medium draft");
        page::runtime::request(page::Action::Save); // Still only a UI action.
        composition.setStorageReady(false);
        flow.invalidateStorage();
        composition.setStorageReady(true);
        lv_tick_inc(60);
        lv_timer_handler();
        composition.tick(false);
        assert(store.writes == 0 && page::state()->view == page::View::Agenda);
        action(page::Action::NewEvent);
        agenda_test_ui::interruption = true;
        page::Flow::exit(&flow, parent);
        assert(!page::state() && flow.mapActive());
        composition.setStorageReady(false);
        flow.invalidateStorage();
        assert(!page::state() && !flow.mapActive() && !flow.needsActivation());
        agenda_test_ui::interruption = false;
        composition.setStorageReady(true);
        page::Flow::enter(&flow, parent);
        flow.tick();
        assert(page::state()->view == page::View::Agenda && page::state()->draft.event.id == 0);
        page::Flow::exit(&flow, parent);
        ui::page_profile::set_active_profile(nullptr);
        lv_display_delete(display);
    }
}
