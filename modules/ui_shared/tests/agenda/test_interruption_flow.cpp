#include "popup_test_support.h"
#include "product_composition/agenda_composition.h"
#include "ui/app_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_page_flow.h"
#include "ui/screens/agenda/agenda_page_runtime.h"

#include <cassert>
#include <cstring>
#include <string>

namespace
{
namespace page = ui::agenda::page;
class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord record{};
    unsigned writes = 0;
    bool fail = false;
    agenda::StoreResult readSlot(uint16_t, agenda::EventRecord& out) override
    {
        out = record;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t, const agenda::EventRecord& value) override
    {
        ++writes;
        if (fail) return agenda::StoreResult::IoError;
        record = value;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 1; }
};
class Clock final : public agenda::IAgendaClock
{
    agenda::ClockSample sample() const override { return {1776330000, 0, 0, true}; }
};
void action(page::Action value)
{
    page::runtime::request(value);
    lv_tick_inc(60);
    lv_timer_handler();
}
} // namespace

void testInterruptionFlow()
{
    for (bool wide : {false, true})
    {
        const auto profile = wide ? ui::page_profile::make_pager_profile() : ui::page_profile::make_tdeck_profile();
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

        const auto interrupt = [&]()
        {
            // Exercise the real AppScreen exit/enter callbacks, in the order
            // used by app_runtime. Only the external interruption app is fake.
            agenda_test_ui::interruption = true;
            page::Flow::exit(&flow, parent);
            assert(!page::state() && lv_obj_get_child_count(parent) == 0);
            auto* call = lv_obj_create(parent);
            auto* call_group = lv_group_create();
            set_default_group(call_group);
            flow.tick();
            composition.tick(false); // A submitted save may finish during a call.
            assert(!page::state() && lv_group_get_default() == call_group);
            lv_obj_delete(call);
            set_default_group(nullptr);
            lv_group_delete(call_group);
            agenda_test_ui::interruption = false;
            assert(!flow.needsActivation()); // Never steal focus from the shell.
            flow.tick();
            assert(!page::state());
            page::Flow::enter(&flow, parent);
            flow.tick();
            assert(page::state() && lv_group_get_default() == page::state()->group);
        };

        action(page::Action::NewEvent);
        // Merely focus a field, without activating its picker, then interrupt.
        lv_group_focus_obj(page::state()->editor_widgets.title);
        for (const auto field : {ui::agenda::EditorField::Date, ui::agenda::EditorField::Time,
                                 ui::agenda::EditorField::Reminder, ui::agenda::EditorField::Location,
                                 ui::agenda::EditorField::Repeat})
        {
            lv_group_focus_next(page::state()->group);
            assert(page::state()->draft.focus == field);
            interrupt();
            assert(page::state()->draft.focus == field);
            assert(lv_group_get_focused(page::state()->group) == page::state()->editor_widgets.focus);
        }
        lv_textarea_set_text(page::state()->editor_widgets.title, "Draft interrupted before save");
        lv_textarea_set_text(page::state()->editor_widgets.note, "Not committed");
        interrupt();
        assert(page::state()->view == page::View::Editor);
        assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Draft interrupted before save") == 0);
        assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.note), "Not committed") == 0);
        assert(store.writes == 0);

        // LVGL limits character count, storage limits UTF-8 bytes. Preserve the
        // largest permitted live text even when it is not a valid saved draft.
        std::string title, note;
        for (unsigned i = 0; i < 39; ++i) title += "\xF0\x9F\x8F\x95";
        for (unsigned i = 0; i < 79; ++i) note += "\xF0\x9F\x8F\x95";
        lv_textarea_set_text(page::state()->editor_widgets.title, title.c_str());
        lv_textarea_set_text(page::state()->editor_widgets.note, note.c_str());
        action(page::Action::Back);
        assert(page::state()->confirm_discard);
        interrupt();
        assert(page::state()->confirm_discard && page::state()->editor_widgets.confirm_root);
        action(page::Action::KeepEditing);
        assert(title == lv_textarea_get_text(page::state()->editor_widgets.title));
        assert(note == lv_textarea_get_text(page::state()->editor_widgets.note));
        action(page::Action::Save);
        assert(page::state()->error && !page::state()->awaiting_command && store.writes == 0);

        lv_textarea_set_text(page::state()->editor_widgets.title, "Recovered draft");
        lv_textarea_set_text(page::state()->editor_widgets.note, "Recovered note");
        action(page::Action::PickTime);
        const int32_t values[] = {7, 23, 9, 41};
        for (unsigned i = 0; i < 4; ++i) lv_spinbox_set_value(page::state()->editor_widgets.clock_fields[i], values[i]);
        lv_obj_add_state(page::state()->editor_widgets.end_enabled, LV_STATE_CHECKED);
        interrupt();
        assert(page::state()->view == page::View::TimePicker);
        for (unsigned i = 0; i < 4; ++i) assert(lv_spinbox_get_value(page::state()->editor_widgets.clock_fields[i]) == values[i]);
        assert(lv_obj_has_state(page::state()->editor_widgets.end_enabled, LV_STATE_CHECKED));
        action(page::Action::Back);
        action(page::Action::PickDate);
        action(page::Action::NextMonth);
        const auto date = page::state()->picker.date;
        interrupt();
        assert(page::state()->view == page::View::DatePicker);
        assert(page::state()->picker.date.year == date.year && page::state()->picker.date.month == date.month);
        action(page::Action::Back);

        // A prepared reminder navigation must be cancelled, not resumed as a
        // background Map launch after the high-priority app has taken over.
        assert(!flow.prepareDestination(0, 0));
        interrupt();
        flow.finishDestination(false);
        assert(page::state()->view == page::View::Editor && !flow.mapActive());
        assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Recovered draft") == 0);

        action(page::Action::PickLocation);
        action(page::Action::ChooseOnMap);
        flow.tick();
        assert(!page::state() && flow.mapActive());
        interrupt();
        assert(page::state()->view == page::View::Editor && !flow.mapActive());
        assert(std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.title), "Recovered draft") == 0);
        assert(store.writes == 0);

        store.fail = true;
        action(page::Action::Save);
        assert(page::state()->awaiting_command);
        interrupt();
        lv_tick_inc(60);
        lv_timer_handler();
        assert(!page::state()->awaiting_command && page::state()->error && store.writes == 1);
        assert(page::state()->view == page::View::Editor);
        store.fail = false;
        action(page::Action::Save);
        interrupt();
        lv_tick_inc(60);
        lv_timer_handler();
        assert(page::state()->view == page::View::Agenda && store.writes == 2);
        assert(std::strcmp(store.record.title, "Recovered draft") == 0);

        page::Flow::exit(&flow, parent); // Normal exit releases all return state.
        assert(!flow.mapActive() && !page::state());
        ui::page_profile::set_active_profile(nullptr);
        lv_display_delete(display);
    }
}
