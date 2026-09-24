#include "esp32_lvgl_arduino_agenda.h"
#include "esp32_lvgl_runtime_config.h"
#include "platform/esp/arduino_common/agenda_clock.h"
#include "platform/esp/arduino_common/storage/agenda_storage.h"
#include "platform/esp/arduino_common/storage/waypoint_storage.h"
#include "platform/esp/common/storage/agenda_file_store.h"
#include "popup_test_support.h"
#include "product_composition/agenda_target.h"
#include "ui/app_runtime.h"
#include "ui/app_screen.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_reminder_popup.h"
#include "ui_presentation/waypoint/waypoint_model.h"

#include <cassert>
#include <cstdio>
#include <cstring>

void cancelTestMap();
void returnFromTestMapTarget();
void assertTestMapTarget(double latitude, double longitude);

lv_obj_t* main_screen = nullptr;

namespace
{
bool menu_exit_requested = false;
unsigned reminder_tones = 0;
bool storage_available = false;
uint32_t storage_session = 1;
AppScreen* active_app = nullptr;
::agenda::ClockSample target_time{1776330000, 0, 0, true};
lv_obj_t* popup_button(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0)
    {
        auto* parent = lv_obj_get_parent(root);
        if (lv_obj_check_type(parent, &lv_button_class)) return parent;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = popup_button(lv_obj_get_child(root, i), text)) return found;
    return nullptr;
}
} // namespace
void ui_request_exit_to_menu() { menu_exit_requested = true; }
namespace platform::ui::device
{
void play_message_tone() { ++reminder_tones; }
} // namespace platform::ui::device
// Shell boundary substitute: keep exit-before-enter ordering explicit. The
// firmware still calls the real app_runtime implementation, not this helper.
AppScreen* ui_get_active_app() { return active_app; }
void ui_switch_to_app(AppScreen* app, lv_obj_t* parent)
{
    assert(!ui::agenda::reminder_popup::visible());
    assert(!agenda_test_ui::interruption && !agenda_test_ui::transition);
    if (active_app && active_app != app) active_app->exit(parent);
    if (app) app->enter(parent);
    active_app = app;
}
namespace ui::presentation_sources
{
bool RuntimeGpsStatusSource::buildGpsStatusSnapshot(ui::gps::GpsStatusSnapshot&) const { return false; }
RuntimeGpsStatusSource& runtime_gps_status_source()
{
    static RuntimeGpsStatusSource source;
    return source;
}
} // namespace ui::presentation_sources
namespace platform::esp::arduino_common
{
::agenda::ClockSample AgendaClock::sample() const { return target_time; }
namespace storage
{
bool agenda_storage_available() { return storage_available; }
uint32_t agenda_storage_session() { return storage_session; }
bool prepare_agenda_storage() { return storage_available; }
} // namespace storage
} // namespace platform::esp::arduino_common

void testTargetRoot()
{
    namespace runtime = trailmate::apps::esp32_lvgl::arduino_agenda;
    namespace page = ui::agenda::page;
    namespace storage = platform::esp::arduino_common::storage;
    assert(!runtime::application());
    runtime::tick(); // Safe before startup, including an aborted boot.
    assert(product_composition::targetHasAgenda(trailmate::apps::esp32_lvgl::esp32LvglRuntimeTargetProfile()));
    std::remove(storage::kAgendaFile);
    std::remove(storage::kAgendaInitializationFile);
    std::remove(storage::kWaypointFile);
    std::remove(storage::kWaypointInitializationFile);
    runtime::initialize();
    auto* app = runtime::application();
    assert(app && std::strcmp(app->stable_id(), "calendar") == 0);
    assert(std::strcmp(app->name(), "Agenda") == 0);
    assert(app->icon() && app->icon()->header.w == 64 && app->icon()->header.h == 64);
    runtime::initialize(); // Idempotent; does not replace services or storage.
    assert(runtime::application() == app);

    auto* display = lv_display_create(320, 240);
    auto* screen = lv_screen_active();
    main_screen = screen;
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    app->enter(screen);
    assert(page::state() && !page::state()->snapshot.storage_ready);
    storage_available = true;
    ++target_time.monotonic_seconds;
    active_app = app;
    runtime::tick();
    assert(page::state() && page::state()->snapshot.storage_ready);
    assert(page::state()->host->waypoints && page::state()->host->waypoint_actions);
    waypoint::Record waypoint_draft;
    std::strcpy(waypoint_draft.name, "Session campsite");
    assert(page::state()->host->waypoint_actions->save(waypoint_draft).ok);
    assert(page::state()->host->waypoints->commandResult().state == ui::waypoint::CommandState::Pending);
    // Closing a page completes an accepted write before releasing its session.
    app->exit(screen);
    app->enter(screen);
    waypoint::Page waypoint_page;
    assert(page::state()->host->waypoints->page(0, 5, waypoint_page) == waypoint::Result::Ok);
    assert(waypoint_page.count == 1 && !std::strcmp(waypoint_page.rows[0].name, "Session campsite"));
    const auto waypoint_action = [](page::Action action)
    {
        page::runtime::request(action);
        lv_tick_inc(60);
        lv_timer_handler();
    };
    waypoint_action(page::Action::NewEvent);
    waypoint_action(page::Action::PickLocation);
    waypoint_action(page::Action::PickWaypoint);
    assert(page::state()->view == page::View::WaypointPicker && page::state()->waypoint_page.count == 1);
    waypoint_action(page::Action::SelectWaypoint);
    assert(page::state()->view == page::View::Editor);
    assert(page::state()->draft.event.location_type == agenda::LocationType::Waypoint);
    assert(!std::strcmp(page::state()->draft.event.location_name, "Session campsite"));
    assert(page::state()->host->source->commandResult().state == ui::agenda::CommandState::Idle);
    waypoint_action(page::Action::PickLocation);
    waypoint_action(page::Action::PickWaypoint);
    waypoint_action(page::Action::NameWaypoint);
    assert(page::state()->view == page::View::WaypointName);
    lv_textarea_set_text(page::state()->editor_widgets.title, "Named through UI");
    agenda_test_ui::interruption = true;
    app->exit(screen);
    agenda_test_ui::interruption = false;
    ui_switch_to_app(app, screen);
    runtime::tick();
    assert(page::state()->view == page::View::WaypointName);
    assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Named through UI"));
    waypoint_action(page::Action::SaveWaypoint);
    assert(page::state()->awaiting_command && page::state()->view == page::View::WaypointName);
    agenda_test_ui::interruption = true;
    app->exit(screen);
    runtime::tick(); // Complete the submitted waypoint write during interruption.
    agenda_test_ui::interruption = false;
    ui_switch_to_app(app, screen);
    runtime::tick();
    lv_tick_inc(60);
    lv_timer_handler();
    assert(page::state()->view == page::View::Editor && !page::state()->awaiting_command);
    assert(!std::strcmp(page::state()->draft.event.location_name, "Named through UI"));
    assert(page::state()->host->source->commandResult().state == ui::agenda::CommandState::Idle);
    active_app = nullptr;
    waypoint_action(page::Action::Discard);
    for (unsigned index = 0; index < 6; ++index)
    {
        std::snprintf(waypoint_draft.name, sizeof(waypoint_draft.name), "Extra %u", index);
        assert(page::state()->host->waypoint_actions->save(waypoint_draft).ok);
        app->exit(screen);
        app->enter(screen);
    }
    waypoint_action(page::Action::NewEvent);
    waypoint_action(page::Action::PickLocation);
    waypoint_action(page::Action::PickWaypoint);
    assert(page::state()->waypoint_page.has_more);
    waypoint_action(page::Action::NextWaypoints);
    const auto saved_cursor = page::state()->waypoint_page.after;
    const auto saved_first = page::state()->waypoint_page.ids[0];
    assert(saved_cursor && saved_first > saved_cursor);
    agenda_test_ui::interruption = true;
    app->exit(screen);
    assert(!page::state());
    agenda_test_ui::interruption = false;
    ui_switch_to_app(app, screen);
    runtime::tick();
    assert(page::state()->view == page::View::WaypointPicker);
    assert(page::state()->waypoint_page.after == saved_cursor);
    assert(page::state()->waypoint_page.ids[0] == saved_first);
    waypoint_action(page::Action::FirstWaypoints);
    assert(!page::state()->waypoint_page.after && page::state()->waypoint_page.ids[0] == 1);
    waypoint_action(page::Action::Back);
    assert(page::state()->view == page::View::LocationPicker);
    waypoint_action(page::Action::Back);
    waypoint_action(page::Action::Discard);
    active_app = nullptr;
    auto* source = page::state()->host->source;
    auto* actions = page::state()->host->actions;
    ui::agenda::AgendaEditorModel draft;
    assert(source->newDraft(draft));
    std::strcpy(draft.event.title, "Persistent radio check");
    draft.event.start_time = target_time.calendar_seconds + 660;
    draft.event.flags = agenda::HasReminder;
    draft.event.reminder_offset_sec = 600;
    assert(actions->save(draft.event).ok);
    assert(source->commandResult().state == ui::agenda::CommandState::Pending);
    app->exit(screen);
    assert(!page::state());
    // The real target root, not a page timer, performs the durable command.
    runtime::tick();
    assert(source->commandResult().state == ui::agenda::CommandState::Succeeded);
    const auto id = source->commandResult().event_id;
    {
        platform::esp::storage::AgendaFileStore reopened(storage::kAgendaFile, storage::kAgendaInitializationFile);
        assert(reopened.begin() == agenda::StoreResult::Ok);
        agenda::EventRecord record;
        assert(reopened.readSlot(0, record) == agenda::StoreResult::Ok);
        assert(record.id == id && std::strcmp(record.title, draft.event.title) == 0);
    }
    app->enter(screen);
    assert(page::state()->snapshot.page.count == 1);
    page::state()->host->navigation.request_exit(nullptr);
    assert(menu_exit_requested);
    app->exit(screen);
    namespace popup = ui::agenda::reminder_popup;
    assert(!page::state() && !popup::visible());
    target_time.calendar_seconds += 60;
    target_time.monotonic_seconds += 60;
    agenda_test_ui::transition = true;
    runtime::tick();
    assert(!popup::visible());
    assert(reminder_tones == 0);
    agenda_test_ui::transition = false;
    runtime::tick();
    assert(popup::visible() && !page::state());
    assert(reminder_tones == 1);
    runtime::tick();
    assert(reminder_tones == 1);
    popup::close();
    runtime::tick();
    assert(popup::visible() && reminder_tones == 1);
    assert(!popup_button(lv_layer_top(), "Navigate"));
    auto* snooze = popup_button(lv_layer_top(), "Snooze 10 min");
    assert(snooze);
    lv_tick_inc(401);
    lv_obj_send_event(snooze, LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    assert(source->commandResult().state == ui::agenda::CommandState::Pending);
    runtime::tick();
    assert(!popup::visible());
    target_time.calendar_seconds += 599;
    target_time.monotonic_seconds += 599;
    runtime::tick();
    assert(!popup::visible());
    ++target_time.calendar_seconds;
    ++target_time.monotonic_seconds;
    runtime::tick();
    assert(popup::visible());
    assert(reminder_tones == 2);
    lv_tick_inc(401);
    auto* done = popup_button(lv_layer_top(), "Done");
    assert(done);
    lv_obj_send_event(done, LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    runtime::tick();
    assert(!popup::visible() && !agenda_test_ui::overlay);
    // A due reminder cannot take the shared command-result slot while the
    // editor still needs to observe its completed save.
    app->enter(screen);
    page::runtime::request(page::Action::NewEvent);
    lv_tick_inc(60);
    lv_timer_handler();
    assert(page::state()->view == page::View::Editor);
    lv_textarea_set_text(page::state()->editor_widgets.title, "Save before reminder");
    page::state()->draft.event.start_time = target_time.calendar_seconds + 601;
    page::state()->draft.event.flags = agenda::HasReminder;
    page::state()->draft.event.reminder_offset_sec = 600;
    page::runtime::request(page::Action::Save);
    runtime::tick();
    assert(!popup::visible()); // Queued page action has not run yet.
    lv_tick_inc(60);
    lv_timer_handler();
    assert(page::state()->awaiting_command);
    runtime::tick();
    const auto saved_sequence = source->commandResult().sequence;
    assert(source->commandResult().state == ui::agenda::CommandState::Succeeded);
    ++target_time.calendar_seconds;
    ++target_time.monotonic_seconds;
    runtime::tick();
    assert(!popup::visible() && page::state()->awaiting_command);
    assert(source->commandResult().sequence == saved_sequence);
    lv_tick_inc(60);
    lv_timer_handler();
    assert(!page::state()->awaiting_command && page::state()->view == page::View::Agenda);
    runtime::tick();
    assert(popup::visible());
    lv_tick_inc(401);
    lv_obj_send_event(popup_button(lv_layer_top(), "Done"), LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    runtime::tick();
    assert(!popup::visible() && lv_group_get_default() == page::state()->group);
    // A reminder becoming due during a temporary Map visit remains scheduled,
    // but cannot steal the Map's input group or the editor return context.
    assert(source->newDraft(draft));
    std::strcpy(draft.event.title, "Reminder during map");
    draft.event.start_time = target_time.calendar_seconds + 60;
    draft.event.flags = agenda::HasReminder;
    assert(actions->save(draft.event).ok);
    runtime::tick();
    page::runtime::request(page::Action::NewEvent);
    lv_tick_inc(60);
    lv_timer_handler();
    lv_textarea_set_text(page::state()->editor_widgets.title, "Uncommitted map draft");
    page::runtime::request(page::Action::PickLocation);
    lv_tick_inc(60);
    lv_timer_handler();
    page::runtime::request(page::Action::ChooseOnMap);
    lv_tick_inc(60);
    lv_timer_handler();
    runtime::tick();
    assert(!page::state() && !popup::visible());
    target_time.calendar_seconds += 60;
    target_time.monotonic_seconds += 60;
    runtime::tick();
    assert(!page::state() && !popup::visible());
    cancelTestMap();
    runtime::tick();
    assert(page::state() && page::state()->view == page::View::Editor && popup::visible());
    assert(std::strcmp(page::state()->draft.event.title, "Uncommitted map draft") == 0);
    lv_tick_inc(401);
    lv_obj_send_event(popup_button(lv_layer_top(), "Done"), LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    runtime::tick();
    assert(!popup::visible() && lv_group_get_default() == page::state()->group);
    app->exit(screen);

    // A located reminder over another application must leave its tree alive
    // until acceptance, then exit that app through the shell before opening Map.
    struct OtherApp final : AppScreen
    {
        lv_obj_t* tree = nullptr;
        unsigned exits = 0;
        const char* stable_id() const override { return "test-other"; }
        const char* name() const override { return "Other"; }
        const lv_image_dsc_t* icon() const override { return nullptr; }
        void enter(lv_obj_t* parent) override { tree = lv_obj_create(parent); }
        void exit(lv_obj_t*) override
        {
            assert(!popup::visible());
            lv_obj_delete(tree);
            tree = nullptr;
            ++exits;
        }
    } other;
    ui_switch_to_app(&other, screen);
    assert(source->newDraft(draft));
    std::strcpy(draft.event.title, "Navigate from another app");
    draft.event.start_time = target_time.calendar_seconds + 1;
    draft.event.flags = agenda::HasReminder | agenda::HasLocation;
    draft.event.location_type = agenda::LocationType::Coordinate;
    draft.event.latitude_e7 = 248731000;
    draft.event.longitude_e7 = 1180000000;
    assert(actions->save(draft.event).ok);
    runtime::tick();
    ++target_time.calendar_seconds;
    ++target_time.monotonic_seconds;
    runtime::tick();
    assert(popup::visible() && popup_button(lv_layer_top(), "Navigate"));
    lv_tick_inc(401);
    lv_obj_send_event(popup_button(lv_layer_top(), "Navigate"), LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    assert(other.tree && other.exits == 0 && popup::visible());
    runtime::tick();
    assert(!popup::visible() && other.tree && other.exits == 0);
    runtime::tick();
    assert(other.exits == 1 && !other.tree && ui_get_active_app() == app);
    assert(!page::state());
    assertTestMapTarget(24.8731, 118.0);
    returnFromTestMapTarget();
    runtime::tick();
    assert(page::state() && page::state()->view == page::View::Agenda);
    assert(page::state()->request.day_start == page::state()->snapshot.today_start);

    // Navigation from a reminder must not use that reminder's event as the
    // editor draft, or discard text that has not reached the draft yet.
    draft.event.id = 0;
    draft.event.start_time = target_time.calendar_seconds + 1;
    assert(actions->save(draft.event).ok);
    runtime::tick();
    page::runtime::request(page::Action::NewEvent);
    lv_tick_inc(60);
    lv_timer_handler();
    const char* oversized = u8"漢漢漢漢漢漢漢漢漢漢漢漢漢漢";
    lv_textarea_set_text(page::state()->editor_widgets.title, oversized);
    ++target_time.calendar_seconds;
    ++target_time.monotonic_seconds;
    runtime::tick();
    lv_tick_inc(401);
    lv_obj_send_event(popup_button(lv_layer_top(), "Navigate"), LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    assert(popup::visible() && page::state());
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), oversized) == 0);
    popup::close(); // Withdraw presentation; the unconsumed reminder remains.
    lv_textarea_set_text(page::state()->editor_widgets.title, "Keep my unfinished plan");
    lv_textarea_set_text(page::state()->editor_widgets.note, "Not the reminder event");
    runtime::tick();
    assert(popup::visible());
    lv_tick_inc(401);
    lv_obj_send_event(popup_button(lv_layer_top(), "Navigate"), LV_EVENT_CLICKED, nullptr);
    runtime::tick();
    runtime::tick();
    runtime::tick();
    assert(!page::state() && !popup::visible());
    assertTestMapTarget(24.8731, 118.0);
    returnFromTestMapTarget();
    runtime::tick();
    assert(page::state()->view == page::View::Editor && page::state()->draft.dirty);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Keep my unfinished plan") == 0);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.note), "Not the reminder event") == 0);
    assert(page::state()->draft.event.id == 0 && !(page::state()->draft.event.flags & agenda::HasLocation));
    // If the shell resumes Calendar, keep the recovery context until re-entry.
    agenda_test_ui::interruption = true;
    app->exit(screen);
    active_app = nullptr;
    runtime::tick();
    assert(!page::state());
    agenda_test_ui::interruption = false;
    ui_switch_to_app(app, screen);
    runtime::tick();
    assert(page::state()->view == page::View::Editor);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Keep my unfinished plan") == 0);

    // An interruption may supersede an already-pending exit-to-menu. In that
    // case the shell does not resume Calendar: release recovery storage, and
    // do not resurrect the discarded session on a later explicit app opening.
    agenda_test_ui::interruption = true;
    app->exit(screen);
    other.enter(screen);
    active_app = &other;
    runtime::tick();
    agenda_test_ui::interruption = false;
    runtime::tick();
    assert(!page::state() && active_app == &other);
    ui_switch_to_app(app, screen);
    runtime::tick();
    assert(page::state()->view == page::View::Agenda);
    app->exit(screen);
    active_app = nullptr;
    // Loss must cancel commands before the composition pump. Recovery must not
    // replay them, even if the same logical store becomes available again.
    assert(source->newDraft(draft));
    std::strcpy(draft.event.title, "Cancelled by media loss");
    assert(actions->save(draft.event).ok);
    storage_available = false;
    runtime::tick();
    assert(source->commandResult().state == ui::agenda::CommandState::Failed);
    assert(!source->newDraft(draft));
    storage_available = true;
    ++target_time.monotonic_seconds;
    runtime::tick();
    assert(source->commandResult().state == ui::agenda::CommandState::Failed);
    assert(source->newDraft(draft));
    std::strcpy(draft.event.title, "Do not replay after rapid remount");
    assert(actions->save(draft.event).ok);
    ++storage_session; // Available remained true across the observed ticks.
    runtime::tick();
    assert(source->commandResult().state == ui::agenda::CommandState::Failed);
    assert(source->newDraft(draft));
    storage_available = false;
    runtime::tick();
    assert(!popup::visible());
    main_screen = nullptr;
    lv_display_delete(display);
    assert(std::remove(storage::kAgendaFile) == 0);
    assert(std::remove(storage::kWaypointFile) == 0);
    std::puts("Agenda target root: durable command and page-closed reminder/snooze/done passed");
}
