#include "popup_test_support.h"
#include "product_composition/agenda_composition.h"
#include "ui/app_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_page_components.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_reminder_popup.h"
#include "ui_presentation/waypoint/waypoint_model.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
namespace page = ui::agenda::page;
uint16_t framebuffer[480 * 240];
uint16_t draw_buffer[480 * 40];
int display_width = 0;

class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord records[8]{};
    bool fail = false;
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        out = records[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t slot, const agenda::EventRecord& record) override
    {
        if (fail) return agenda::StoreResult::IoError;
        records[slot] = record;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 8; }
};
class WaypointStore final : public waypoint::IStore
{
  public:
    uint16_t slotCount() const override { return 7; }
    waypoint::Result read(uint16_t slot, waypoint::Record& out) override
    {
        out = {};
        out.id = slot + 1;
        std::snprintf(out.name, sizeof(out.name), "Campsite %u", unsigned(out.id));
        return waypoint::Result::Ok;
    }
    waypoint::Result create(const waypoint::Record&, uint32_t&) override { return waypoint::Result::IoError; }
    waypoint::Result update(const waypoint::Record&) override { return waypoint::Result::IoError; }
    waypoint::Result remove(uint32_t) override { return waypoint::Result::IoError; }
};
class Clock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample value{1776330000, 0, 0, true};
    agenda::ClockSample sample() const override { return value; }
    void advance(unsigned seconds)
    {
        value.calendar_seconds += seconds;
        value.monotonic_seconds += seconds;
    }
};
class LocationSource final : public ui::agenda::IAgendaLocationSource
{
  public:
    bool available = false;
    bool currentLocation(ui::agenda::AgendaCoordinate& out) const override
    {
        if (!available) return false;
        out = {248731000, 1180000000};
        return true;
    }
};

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels)
{
    const auto* source = reinterpret_cast<uint16_t*>(pixels);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * display_width + x] = *source++;
    lv_display_flush_ready(display);
}
lv_obj_t* find_label(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* result = find_label(lv_obj_get_child(root, i), text)) return result;
    return nullptr;
}
lv_obj_t* find_button(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_button_class) && find_label(root, text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* result = find_button(lv_obj_get_child(root, i), text)) return result;
    return nullptr;
}
void click(const char* text)
{
    auto* button = find_button(page::state()->root, text);
    if (!button) std::fprintf(stderr, "Missing button '%s', width=%d view=%u pending=%u waiting=%u confirm=%u\n",
                              text, display_width, unsigned(page::state()->view), unsigned(page::state()->pending),
                              unsigned(page::state()->awaiting_command), unsigned(page::state()->confirm_delete));
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}
void pump(product_composition::AgendaComposition& composition)
{
    composition.tick(false);
    lv_tick_inc(60);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}
void verify_buttons(lv_obj_t* object, int width, int height)
{
    if (lv_obj_check_type(object, &lv_button_class))
    {
        lv_area_t bounds;
        lv_obj_get_coords(object, &bounds);
        assert(bounds.x1 >= 0 && bounds.y1 >= 0 && bounds.x2 < width && bounds.y2 < height);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        verify_buttons(lv_obj_get_child(object, i), width, height);
}
void screenshot(lv_display_t* display, int width, int height, const char* view = "home")
{
    lv_refr_now(display);
    char path[64];
    std::snprintf(path, sizeof(path), "agenda-%s-%dx%d.bmp", view, width, height);
    auto* file = std::fopen(path, "wb");
    assert(file);
    const uint32_t stride = (width * 3 + 3) & ~3;
    const uint32_t bytes = 54 + stride * height;
    uint8_t header[54]{};
    auto put32 = [&](unsigned offset, uint32_t value)
    { for (unsigned i = 0; i < 4; ++i) header[offset + i] = static_cast<uint8_t>(value >> (8 * i)); };
    header[0] = 'B';
    header[1] = 'M';
    put32(2, bytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    header[26] = 1;
    header[28] = 24;
    assert(std::fwrite(header, 1, sizeof(header), file) == sizeof(header));
    for (int y = height - 1; y >= 0; --y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto color = framebuffer[y * width + x];
            const uint8_t rgb[3] = {static_cast<uint8_t>((color & 31) * 255 / 31),
                                    static_cast<uint8_t>(((color >> 5) & 63) * 255 / 63),
                                    static_cast<uint8_t>((color >> 11) * 255 / 31)};
            assert(std::fwrite(rgb, 1, 3, file) == 3);
        }
        for (uint32_t pad = width * 3; pad < stride; ++pad) std::fputc(0, file);
    }
    assert(std::fclose(file) == 0);
}

void run(int width, int height, const ui::page_profile::PageLayoutProfile& profile)
{
    display_width = width;
    auto* display = lv_display_create(width, height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    ui::page_profile::set_active_profile(&profile);
    auto* screen = lv_screen_active();
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    Store store;
    Clock clock;
    const int64_t day = clock.sample().calendar_seconds / 86400 * 86400;
    for (unsigned i = 0; i < 8; ++i)
    {
        auto& event = store.records[i];
        event.id = i + 1;
        event.state = agenda::RecordState::Active;
        event.start_time = day + 34200 + i * 3600;
        std::snprintf(event.title, sizeof(event.title), "Radio check %u", i + 1);
        if (i == 1)
        {
            event.flags = agenda::HasLocation;
            event.location_type = agenda::LocationType::Coordinate;
            std::strcpy(event.location_name, "Pine Creek");
        }
    }
    product_composition::AgendaComposition composition(store, clock);
    ui::workspace::PresentationWorkspace workspace;
    composition.bind(workspace);
    composition.setStorageReady(true);
    bool exited = false;
    LocationSource locations;
    page::Host host{{&exited, [](void* context)
                     {
                         *static_cast<bool*>(context) = true;
                         page::exit(nullptr, nullptr);
                     }},
                    workspace.agenda,
                    workspace.agenda};
    host.locations = &locations;
    WaypointStore waypoint_store;
    ui::waypoint::Model waypoints(waypoint_store);
    waypoints.setReady(true);
    host.waypoints = &waypoints;
    host.waypoint_actions = &waypoints;
    host.request_target = [](void*, const ui::agenda::AgendaEditorModel&)
    { return false; };
    page::enter(&host, screen);
    assert(page::state() && page::state()->view == page::View::Agenda);
    pump(composition);
    verify_buttons(page::state()->root, width, height);
    assert(find_label(page::state()->root, "Radio check 1"));
    assert(page::state()->snapshot.page.has_more);
    screenshot(display, width, height);
    // Real group traversal includes TopBar Back, even on the encoder profile.
    auto* back = page::state()->top_bar.back_btn;
    lv_group_focus_obj(back);
    struct KeyPayload
    {
        uint32_t code;
        uint8_t guard[512];
    } down{LV_KEY_DOWN, {}};
    const auto original_key = down;
    lv_obj_send_event(back, LV_EVENT_KEY, &down.code);
    assert(std::memcmp(&down, &original_key, sizeof(down)) == 0);
    assert(lv_group_get_focused(page::state()->group) != back);
    for (unsigned i = 0; i < 20 && lv_group_get_focused(page::state()->group) != back; ++i)
        lv_group_focus_next(page::state()->group);
    assert(lv_group_get_focused(page::state()->group) == back);
    const auto last = page::state()->snapshot.page.rows[page::state()->snapshot.page.count - 1].occurrence.event_id;
    click("More");
    pump(composition);
    assert(page::state()->snapshot.page.rows[0].occurrence.event_id == last + 1);
    click("Today");
    pump(composition);
    click("Radio check 1");
    assert(page::state()->view == page::View::Agenda); // No tree mutation in callback.
    pump(composition);
    assert(page::state()->view == page::View::Detail);
    assert(!find_label(page::state()->root, "Reminder"));
    assert(!find_label(page::state()->root, "Repeat"));
    assert(!find_label(page::state()->root, "Navigate"));
    // All optional fields must coexist without covering the fixed action row.
    auto& detail_event = page::state()->draft.event;
    detail_event.flags |= agenda::HasReminder | agenda::HasNote | agenda::HasLocation;
    detail_event.reminder_offset_sec = 600;
    detail_event.repeat = agenda::Repeat::Weekly;
    detail_event.location_type = agenda::LocationType::Coordinate;
    std::strcpy(detail_event.location_name, "Pine Creek");
    std::strcpy(detail_event.note, "Check the water source, radio batteries and weather before leaving the campsite.");
    page::components::render();
    lv_obj_update_layout(page::state()->root);
    assert(find_label(page::state()->root, "Reminder"));
    assert(find_label(page::state()->root, "10 min before"));
    assert(find_label(page::state()->root, "Repeat"));
    assert(find_label(page::state()->root, "Weekly"));
    assert(find_label(page::state()->root, "Navigate"));
    lv_area_t note_bounds, edit_bounds;
    lv_obj_get_coords(find_label(page::state()->root, detail_event.note), &note_bounds);
    lv_obj_get_coords(find_button(page::state()->root, "Edit"), &edit_bounds);
    assert(note_bounds.y2 < edit_bounds.y1 && note_bounds.y2 > note_bounds.y1);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "detail-all-fields");
    click("Navigate");
    pump(composition);
    assert(page::state()->view == page::View::Detail);
    assert(find_label(page::state()->root, "Map is unavailable on this target."));
    page::state()->error = nullptr;
    detail_event = store.records[0];
    page::components::render();
    click("Delete");
    pump(composition);
    assert(page::state()->confirm_delete);
    store.fail = true;
    click("Delete");
    pump(composition);
    pump(composition);
    assert(find_label(page::state()->root, "Could not save changes"));
    assert(store.records[0].state == agenda::RecordState::Active);
    store.fail = false;
    click("Delete");
    pump(composition);
    pump(composition);
    assert(store.records[0].state == agenda::RecordState::Deleted);
    assert(page::state()->view == page::View::Agenda);

    // Invalid multibyte input is neither silently truncated nor a Back trap.
    click("New");
    pump(composition);
    assert(page::state()->view == page::View::Editor);
    click("Save");
    pump(composition);
    assert(find_label(page::state()->root, "Enter a title and valid date/time"));
    const char* oversized = u8"漢漢漢漢漢漢漢漢漢漢漢漢漢漢";
    auto* original_title = page::state()->editor_widgets.title;
    lv_textarea_set_text(original_title, oversized);
    click("Save");
    pump(composition);
    assert(page::state()->editor_widgets.title == original_title);
    assert(find_label(page::state()->root, "Text exceeds the UTF-8 byte limit"));
    lv_obj_send_event(page::state()->top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
    pump(composition);
    assert(page::state()->confirm_discard);
    click("Keep editing");
    pump(composition);
    assert(page::state()->editor_widgets.title == original_title);
    assert(std::strcmp(lv_textarea_get_text(original_title), oversized) == 0);
    lv_obj_send_event(page::state()->top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
    pump(composition);
    click("Discard");
    pump(composition);
    assert(page::state()->view == page::View::Agenda);

    click("New");
    pump(composition);
    lv_textarea_set_text(page::state()->editor_widgets.title, "Evening radio");
    lv_textarea_set_text(page::state()->editor_widgets.note, "Check battery first");
    screenshot(display, width, height, "editor");
    page::runtime::request(page::Action::PickLocation);
    pump(composition);
    assert(page::state()->view == page::View::LocationPicker);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "location-picker");
    click("Current position");
    pump(composition);
    assert(page::state()->view == page::View::LocationPicker);
    assert(find_label(page::state()->root, "Current position unavailable"));
    assert(!(page::state()->draft.event.flags & agenda::HasLocation));
    locations.available = true;
    click("Current position");
    pump(composition);
    assert(page::state()->view == page::View::Editor);
    assert(page::state()->draft.event.flags & agenda::HasLocation);
    assert(page::state()->draft.event.latitude_e7 == 248731000);
    assert(page::state()->draft.event.longitude_e7 == 1180000000);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Evening radio") == 0);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.note), "Check battery first") == 0);
    assert(store.records[0].state == agenda::RecordState::Deleted); // Still only a draft.
    page::runtime::request(page::Action::PickLocation);
    pump(composition);
    click("Saved waypoint");
    pump(composition);
    assert(page::state()->view == page::View::WaypointPicker);
    assert(page::state()->waypoint_page.count == 5 && page::state()->waypoint_page.has_more);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "waypoint-picker");
    click("More");
    pump(composition);
    assert(page::state()->waypoint_page.count == 2 && !page::state()->waypoint_page.has_more);
    assert(find_label(page::state()->root, "Campsite 6"));
    click("First");
    pump(composition);
    assert(find_label(page::state()->root, "Campsite 1"));
    click("New");
    pump(composition);
    assert(page::state()->view == page::View::WaypointName);
    assert(find_label(page::state()->root, "Name"));
    lv_textarea_set_text(page::state()->editor_widgets.title, "Pine Creek campsite");
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "waypoint-name");
    click("Save");
    pump(composition);
    assert(page::state()->awaiting_command);
    waypoints.pump(); // This fixture rejects writes; the UI must retain the draft.
    pump(composition);
    assert(page::state()->view == page::View::WaypointName && !page::state()->awaiting_command);
    assert(find_label(page::state()->root, "Could not save changes"));
    assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Pine Creek campsite"));
    assert(store.records[0].state == agenda::RecordState::Deleted);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "waypoint-save-error");
    page::runtime::request(page::Action::Back);
    pump(composition);
    assert(page::state()->view == page::View::WaypointPicker);
    page::runtime::request(page::Action::Back);
    pump(composition);
    assert(page::state()->view == page::View::LocationPicker);
    page::runtime::request(page::Action::Back);
    pump(composition);
    assert(page::state()->view == page::View::Editor);
    page::runtime::request(page::Action::PickLocation);
    pump(composition);
    locations.available = false;
    click("Current position");
    pump(composition);
    assert(page::state()->draft.event.latitude_e7 == 248731000); // Failed replacement preserves it.
    page::runtime::request(page::Action::Back);
    pump(composition);
    assert(page::state()->view == page::View::Editor);
    page::runtime::request(page::Action::PickLocation);
    pump(composition);
    click("Remove location");
    pump(composition);
    assert(!(page::state()->draft.event.flags & agenda::HasLocation));
    assert(page::state()->draft.event.location_name[0] == '\0');
    page::runtime::request(page::Action::PickLocation);
    pump(composition);
    locations.available = true;
    click("Current position");
    pump(composition);
    page::runtime::request(page::Action::PickReminder);
    pump(composition);
    assert(page::state()->view == page::View::ReminderPicker);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "reminder-picker");
    click("10 min before");
    pump(composition);
    assert(page::state()->draft.event.reminder_offset_sec == 600);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Evening radio") == 0);
    page::runtime::request(page::Action::PickRepeat);
    pump(composition);
    click("Weekly");
    pump(composition);
    assert(page::state()->draft.event.repeat == agenda::Repeat::Weekly);
    page::runtime::request(page::Action::PickTime);
    pump(composition);
    assert(page::state()->view == page::View::TimePicker);
    lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[0], 18);
    lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[1], 0);
    lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[2], 19);
    lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[3], 0);
    lv_obj_add_state(page::state()->editor_widgets.end_enabled, LV_STATE_CHECKED);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "time-picker");
    click("OK");
    pump(composition);
    assert(page::state()->draft.event.end_time - page::state()->draft.event.start_time == 3600);
    // Equal start/end explicitly represents the same time on the next day.
    page::runtime::request(page::Action::PickTime);
    pump(composition);
    lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[2], 18);
    click("OK");
    pump(composition);
    assert(page::state()->draft.event.end_time - page::state()->draft.event.start_time == 86400);
    page::runtime::request(page::Action::PickTime);
    pump(composition);
    lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[2], 19);
    click("OK");
    pump(composition);
    page::runtime::request(page::Action::PickDate);
    pump(composition);
    assert(page::state()->view == page::View::DatePicker);
    verify_buttons(page::state()->root, width, height);
    screenshot(display, width, height, "date-picker");
    const auto original_date = page::state()->picker.date;
    const uint8_t selected_day = original_date.day < 28 ? original_date.day + 1 : original_date.day - 1;
    auto* days_grid = page::state()->editor_widgets.focus;
    assert(lv_obj_check_type(days_grid, &lv_buttonmatrix_class));
    auto* calendar = lv_obj_get_parent(days_grid);
    assert(lv_obj_check_type(calendar, &lv_calendar_class));
    assert(lv_group_get_focused(page::state()->group) == days_grid);
    assert(lv_obj_get_group(calendar) == nullptr);
    const auto initial_button = lv_buttonmatrix_get_selected_button(days_grid);
    lv_group_set_editing(page::state()->group, true);
    uint32_t right_key = LV_KEY_RIGHT;
    lv_obj_send_event(days_grid, LV_EVENT_KEY, &right_key);
    assert(lv_buttonmatrix_get_selected_button(days_grid) != initial_button);
    assert(lv_group_get_focused(page::state()->group) == days_grid);
    // Exercise the buttonmatrix's bubbling event, not a direct runtime command.
    char day_text[4];
    std::snprintf(day_text, sizeof(day_text), "%u", selected_day);
    bool found_day = false;
    for (unsigned i = 7; i < 49; ++i)
    {
        if (lv_buttonmatrix_has_button_ctrl(days_grid, i, LV_BUTTONMATRIX_CTRL_DISABLED)) continue;
        if (std::strcmp(lv_buttonmatrix_get_button_text(days_grid, i), day_text) != 0) continue;
        lv_buttonmatrix_set_selected_button(days_grid, i);
        lv_obj_send_event(days_grid, LV_EVENT_VALUE_CHANGED, nullptr);
        found_day = true;
        break;
    }
    assert(found_day && page::state()->view == page::View::DatePicker);
    pump(composition);
    assert(page::state()->view == page::View::Editor);
    agenda::CivilTime selected;
    assert(agenda::fromCalendarSeconds(page::state()->draft.event.start_time, selected));
    assert(selected.day == selected_day && selected.hour == 18 && selected.minute == 0);
    assert(page::state()->draft.event.end_time - page::state()->draft.event.start_time == 3600);
    store.fail = true;
    click("Save");
    pump(composition);
    assert(page::state()->awaiting_command);
    assert(lv_obj_has_state(page::state()->editor_widgets.title, LV_STATE_DISABLED));
    assert(lv_obj_has_state(page::state()->editor_widgets.note, LV_STATE_DISABLED));
    pump(composition);
    assert(page::state()->view == page::View::Editor && !page::state()->awaiting_command);
    assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Evening radio") == 0);
    assert(find_label(page::state()->root, "Could not save changes"));
    store.fail = false;
    click("Save");
    pump(composition);
    pump(composition);
    assert(page::state()->view == page::View::Agenda);
    const auto created = workspace.agenda->commandResult().event_id;
    assert(created == 9 && store.records[0].id == created);
    assert(store.records[0].flags == (agenda::HasReminder | agenda::HasNote | agenda::HasEndTime | agenda::HasLocation));
    assert(store.records[0].latitude_e7 == 248731000 && store.records[0].longitude_e7 == 1180000000);
    assert(store.records[0].repeat == agenda::Repeat::Weekly);
    assert(std::strcmp(store.records[0].note, "Check battery first") == 0);
    // Find the new occurrence through the real bounded list, then edit it.
    for (unsigned i = 0; !find_button(page::state()->root, "Evening radio") && i < 8; ++i)
    {
        assert(page::state()->snapshot.page.has_more);
        click("More");
        pump(composition);
    }
    click("Evening radio");
    pump(composition);
    click("Edit");
    pump(composition);
    assert(page::state()->draft.editing_existing);
    assert(page::state()->draft.event.flags & agenda::HasLocation);
    page::runtime::request(page::Action::PickLocation);
    pump(composition);
    click("Remove location");
    pump(composition);
    assert(store.records[0].flags & agenda::HasLocation); // Removal is not persisted until Save.
    lv_textarea_set_text(page::state()->editor_widgets.title, "Updated radio");
    click("Save");
    pump(composition);
    pump(composition);
    assert(store.records[0].id == created && std::strcmp(store.records[0].title, "Updated radio") == 0);
    assert(!(store.records[0].flags & agenda::HasLocation));
    assert(store.records[0].location_type == agenda::LocationType::None);
    lv_obj_send_event(page::state()->top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
    assert(page::state());
    pump(composition);
    assert(exited && !page::state());
    assert(lv_obj_get_child_count(screen) == 0);
    ui::page_profile::set_active_profile(nullptr);
    lv_display_delete(display);
}
void runReminder(int width, int height, const ui::page_profile::PageLayoutProfile& profile)
{
    namespace popup = ui::agenda::reminder_popup;
    display_width = width;
    auto* display = lv_display_create(width, height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    ui::page_profile::set_active_profile(&profile);
    auto* previous_group = lv_group_create();
    set_default_group(previous_group);
    auto* previous_button = lv_button_create(lv_screen_active());
    lv_group_add_obj(previous_group, previous_button);
    lv_group_focus_obj(previous_button);
    lv_group_set_editing(previous_group, true);
    Store store;
    Clock clock;
    for (unsigned i = 0; i < 2; ++i)
    {
        auto& event = store.records[i];
        event.id = i + 1;
        event.state = agenda::RecordState::Active;
        event.start_time = clock.value.calendar_seconds + 601;
        event.flags = agenda::HasReminder | agenda::HasNote;
        event.reminder_offset_sec = 600;
        std::strcpy(event.title, i ? "Leave campsite" : "Radio check");
        std::strcpy(event.note, "Check battery and verify the meeting point.");
    }
    auto& located = store.records[1];
    located.flags |= agenda::HasLocation;
    located.location_type = agenda::LocationType::Coordinate;
    located.latitude_e7 = 248731000;
    located.longitude_e7 = 1180000000;
    std::strcpy(located.location_name, "Pine Creek");
    product_composition::AgendaComposition composition(store, clock);
    ui::workspace::PresentationWorkspace workspace;
    composition.bind(workspace);
    composition.setStorageReady(true);
    struct Navigation
    {
        bool allow = false;
        bool prepared = false;
        bool accepted = false;
        unsigned cancellations = 0;
    } navigation;
    popup::Host host{workspace.agenda, workspace.agenda, workspace.agenda_reminders};
    host.navigation_context = &navigation;
    host.prepare_navigation = [](void* context, int32_t latitude, int32_t longitude) -> const char*
    {
        auto& state = *static_cast<Navigation*>(context);
        assert(popup::visible()); // Prepare must not destroy the current modal.
        assert(latitude == 248731000 && longitude == 1180000000);
        if (!state.allow) return "Map is unavailable on this target.";
        assert(!state.prepared);
        state.prepared = true;
        return nullptr;
    };
    host.finish_navigation = [](void* context, bool accepted)
    {
        auto& state = *static_cast<Navigation*>(context);
        assert(state.prepared);
        if (accepted) assert(!popup::visible());
        state.prepared = false;
        state.accepted = accepted;
        if (!accepted) ++state.cancellations;
    };
    const auto tick = [&]()
    {
        const bool allowed = popup::canPresent();
        composition.tick(allowed && !popup::visible());
        popup::tick(host, allowed);
        lv_obj_update_layout(lv_layer_top());
    };
    const auto action = [&](const char* text)
    {
        auto* button = find_button(lv_layer_top(), text);
        assert(button);
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    };
    tick();
    clock.advance(1);
    agenda_test_ui::overlay = true;
    tick();
    assert(!popup::visible()); // Wait for an existing modal, not a second overlay.
    agenda_test_ui::overlay = false;
    tick();
    assert(popup::visible() && !page::state());
    assert(lv_group_get_default() != previous_group);
    assert(!find_button(lv_layer_top(), "Navigate"));
    struct KeyPayload
    {
        uint32_t code;
        uint8_t guard[512];
    } next{LV_KEY_NEXT, {}};
    const auto original_key = next;
    auto* initial_focus = lv_group_get_focused(lv_group_get_default());
    lv_obj_send_event(initial_focus, LV_EVENT_KEY, &next.code);
    assert(std::memcmp(&next, &original_key, sizeof(next)) == 0);
    assert(lv_group_get_focused(lv_group_get_default()) != initial_focus);
    verify_buttons(lv_layer_top(), width, height);
    screenshot(display, width, height, "reminder");
    action("Done"); // In-flight activation is ignored for the first 400 ms.
    tick();
    assert(workspace.agenda->commandResult().state == ui::agenda::CommandState::Idle);
    lv_tick_inc(401);
    action("Snooze 10 min");
    tick();
    assert(workspace.agenda->commandResult().state == ui::agenda::CommandState::Pending && popup::visible());
    tick();
    assert(!popup::visible() && lv_group_get_default() == previous_group);
    assert(lv_group_get_focused(previous_group) == previous_button && lv_group_get_editing(previous_group));
    tick(); // The other simultaneous reminder was not consumed by snoozing.
    assert(popup::visible() && find_button(lv_layer_top(), "Navigate"));
    verify_buttons(lv_layer_top(), width, height);
    const char* action_labels[] = {"Navigate", "Snooze 10 min", "Done"};
    for (const char* text : action_labels)
    {
        auto* control = find_button(lv_layer_top(), text);
        auto* caption = find_label(control, text);
        assert(caption);
        lv_area_t button_bounds;
        lv_area_t caption_bounds;
        lv_obj_get_coords(control, &button_bounds);
        lv_obj_get_coords(caption, &caption_bounds);
        assert(caption_bounds.y1 >= button_bounds.y1 + 3);
        assert(caption_bounds.y2 <= button_bounds.y2 - 3);
        assert(caption_bounds.x1 >= button_bounds.x1 + 3);
        assert(caption_bounds.x2 <= button_bounds.x2 - 3);
    }
    screenshot(display, width, height, "reminder-location");
    lv_tick_inc(401);
    action("Snooze 10 min");
    tick();
    tick();
    assert(popup::visible());
    assert(find_label(lv_layer_top(), "Another reminder is already snoozed"));
    lv_area_t status_bounds;
    lv_area_t action_bounds;
    lv_obj_get_coords(find_label(lv_layer_top(), "Another reminder is already snoozed"), &status_bounds);
    lv_obj_get_coords(find_button(lv_layer_top(), "Snooze 10 min"), &action_bounds);
    assert(status_bounds.y2 < action_bounds.y1);
    screenshot(display, width, height, "reminder-snooze-full");
    action("Navigate");
    tick();
    assert(popup::visible() && !navigation.prepared && !navigation.accepted);
    assert(find_label(lv_layer_top(), "Map is unavailable on this target."));
    navigation.allow = true;
    action("Navigate");
    tick();
    assert(navigation.prepared && !navigation.accepted && popup::visible());
    tick();
    assert(navigation.accepted && !navigation.prepared && !popup::visible());
    clock.advance(599);
    tick();
    assert(!popup::visible());
    clock.advance(1);
    tick();
    assert(popup::visible() && find_label(lv_layer_top(), "Radio check"));

    // A call takes focus and destroys the interrupted page's group. Closing
    // the reminder must neither dereference the old group nor clear call state.
    agenda_test_ui::interruption = true;
    auto* call_group = lv_group_create();
    set_default_group(call_group);
    lv_group_delete(previous_group);
    previous_group = nullptr;
    tick();
    assert(!popup::visible() && agenda_test_ui::overlay);
    assert(lv_group_get_default() == call_group);
    agenda_test_ui::interruption = false;
    agenda_test_ui::overlay = false;
    tick();
    assert(popup::visible()); // Same snoozed reminder is still pending.
    lv_tick_inc(401);
    action("Done");
    tick();
    tick();
    assert(!popup::visible() && lv_group_get_default() == call_group && !agenda_test_ui::overlay);
    tick();
    assert(!popup::visible());

    // A queued click cannot dismiss a new presentation after a record change.
    agenda::EventRecord draft = store.records[0];
    draft.id = 0;
    draft.start_time = clock.value.calendar_seconds + 601;
    assert(workspace.agenda->save(draft).ok);
    tick();
    const auto created = workspace.agenda->commandResult().event_id;
    clock.advance(1);
    tick();
    assert(popup::visible());
    lv_tick_inc(401);
    action("Done");
    assert(workspace.agenda->remove(created).ok);
    tick();
    assert(!popup::visible());
    assert(workspace.agenda->commandResult().event_id == created);
    // Failed command submission and a higher-priority interruption must both
    // release a prepared route. Neither is permission to switch applications.
    draft = store.records[1];
    draft.id = 0;
    draft.start_time = clock.value.calendar_seconds + 601;
    assert(workspace.agenda->save(draft).ok);
    tick();
    clock.advance(1);
    tick();
    assert(popup::visible() && find_button(lv_layer_top(), "Navigate"));
    navigation.accepted = false;
    lv_tick_inc(401);
    action("Navigate");
    assert(workspace.agenda->save(draft).ok); // Occupy the one command slot.
    popup::tick(host, true);
    assert(popup::visible() && !navigation.prepared && !navigation.accepted);
    assert(navigation.cancellations == 1);
    tick();
    if (!popup::visible()) tick();
    lv_tick_inc(401);
    action("Navigate");
    popup::tick(host, true);
    assert(navigation.prepared);
    agenda_test_ui::interruption = true;
    popup::tick(host, false);
    assert(!popup::visible() && !navigation.prepared && !navigation.accepted);
    assert(navigation.cancellations == 2);
    agenda_test_ui::interruption = false;
    agenda_test_ui::overlay = false;
    set_default_group(nullptr);
    lv_group_delete(call_group);
    ui::page_profile::set_active_profile(nullptr);
    lv_display_delete(display);
}
} // namespace

void testTargetRoot();
void testMapFlow();
void testAgendaFontScope();
void testInterruptionFlow();
void testTouchAgenda();

int main()
{
    lv_init();
    testAgendaFontScope();
    const auto pager = ui::page_profile::make_pager_profile();
    run(480, 222, pager);
    const auto deck = ui::page_profile::make_tdeck_profile();
    run(320, 240, deck);
    runReminder(480, 222, pager);
    runReminder(320, 240, deck);
    testTargetRoot();
    testMapFlow();
    testInterruptionFlow();
    testTouchAgenda();
    std::puts("Agenda UI: 480x222 and 320x240 layout, input, CRUD, pickers and save-failure recovery passed");
}
