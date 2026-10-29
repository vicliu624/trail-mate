#include "app/linux_app_facade.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/audio/audio_focus.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/device_runtime.h"
#include "ui/agenda_runtime_binding.h"
#include "ui/app_catalog_builder.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_reminder_popup.h"
#include "ui/screens/agenda/agenda_state.h"
#include "ui/startup_ui_shell.h"
#include "ui_presentation/waypoint/waypoint_model.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
namespace page = ui::agenda::page;
uint32_t ticks()
{
    static const auto start = std::chrono::steady_clock::now();
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}
struct Clock final : agenda::IAgendaClock
{
    mutable std::mutex mutex;
    agenda::ClockSample value;
    Clock()
    {
        assert(agenda::toCalendarSeconds({2026, 10, 21, 12, 0, 0}, value.calendar_seconds));
        value.valid = true;
    }
    agenda::ClockSample sample() const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return value;
    }
    void advance(unsigned seconds)
    {
        std::lock_guard<std::mutex> lock(mutex);
        value.calendar_seconds += seconds;
        value.monotonic_seconds += seconds;
    }
};
lv_obj_t* label(lv_obj_t* parent, const char* text)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(parent, &lv_label_class) && !std::strcmp(lv_label_get_text(parent), text)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto* result = label(lv_obj_get_child(parent, i), text)) return result;
    return nullptr;
}
int main(int argc, char** argv)
{
    char root[] = "/tmp/trailmate-agenda-ui-XXXXXX";
    assert(mkdtemp(root));
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root, 1);
    setenv("TRAIL_MATE_SD_ROOT", (std::filesystem::path(root) / "sd").c_str(), 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    setenv("TRAIL_MATE_OSM_TILE_URL", "http://127.0.0.1:1/{z}/{x}/{y}.png", 1);
    platform::linux_runtime::PacketRadioHardware radio;
    radio.driver = "none";
    assert(platform::linux_runtime::configure_packet_radio(radio));
    // Agenda rendering must not connect to default public gateways or retain
    // peer events arriving after its last UI dispatch during teardown.
    assert(platform::linux_runtime::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
    Clock clock;
    std::unique_ptr<trailmate::linux_ui::AgendaRuntimeBinding> binding;
    lv_init();
    lv_tick_set_cb(ticks);
    {
        platform::desktop::DeviceOptions options;
        options.title = "Agenda integration";
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        options.hardware_keyboard = true;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        platform::ui::device::set_message_tone_volume(0);
        binding = std::make_unique<trailmate::linux_ui::AgendaRuntimeBinding>(platform::linux_runtime::LinuxAgendaOptions{platform::linux_runtime::sqlite_database_path(), &clock, {}});
        ui::app_catalog_builder::FeatureFlags flags;
        flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
        flags.calendar_app = binding->application();
        flags.map_markers = binding->markers();
        flags.include_energy_sweep = flags.include_walkie_talkie = flags.include_sstv = false;
        ui::startup_ui_shell::Hooks hooks;
        hooks.apps = ui::app_catalog_builder::build(flags);
        hooks.ux_pack_id = "t_display_p4_touch";
        assert(ui::catalogAt(hooks.apps, 0) == binding->application());
        assert(ui::startup_ui_shell::beginBootUi(hooks, false, "Agenda test"));
        ui::startup_ui_shell::prepareBootResources();
        assert(ui::startup_ui_shell::initializeMenuSkeleton(hooks) && ui::startup_ui_shell::finalizeStartup(hooks, false));
        const auto step = [&]
        { assert(host.pump()); facade.updateCoreServices(); facade.tickEventRuntime(); facade.dispatchPendingEvents(); binding->tick(); lv_timer_handler(); ui::i18n::on_lvgl_frame_completed(); host.present(); std::this_thread::sleep_for(5ms); };
        const auto until = [&](auto predicate)
        { const auto deadline = std::chrono::steady_clock::now()+12s; do { step(); assert(std::chrono::steady_clock::now()<deadline); } while(!predicate()); };
        const auto click = [&](const char* text, lv_obj_t* parent)
        {
            auto* object = label(parent, text);
            assert(object);
            while (object && !lv_obj_check_type(object, &lv_button_class)) object = lv_obj_get_parent(object);
            assert(object && !lv_obj_has_state(object, LV_STATE_DISABLED));
            lv_obj_scroll_to_view(object, LV_ANIM_OFF);
            lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
        };
        const auto capture = [&](const char* name)
        { if(argc>1) { lv_refr_now(nullptr); host.present(); std::filesystem::create_directories(argv[1]); host.saveScreenshot((std::filesystem::path(argv[1])/name).string()); } };
        until([&]
              { return binding->source()->markerStatus().ready; });
        until([&]
              { return !label(lv_layer_top(), "Agenda test"); });
        assert(ui_open_app(binding->application()));
        until([&]
              { return page::state() && label(page::state()->root, "New"); });
        capture("agenda-home.bmp");
        click("New", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Editor; });
        lv_textarea_set_text(page::state()->editor_widgets.title, "New event");
        lv_textarea_set_text(page::state()->editor_widgets.note, "中文 😀 note");
        // Supply the coordinate returned by the Map/GPS presentation boundary;
        // all waypoint saving/selection below uses actual shared UI actions.
        assert(ui::agenda::applyCoordinate(page::state()->draft, {225432100, 1141234567}));
        page::runtime::request(page::Action::PickLocation);
        until([&]
              { return page::state()->view == page::View::LocationPicker; });
        click("Saved waypoint", page::state()->root);
        until([&]
              { return page::state()->view == page::View::WaypointPicker; });
        waypoint::Page empty_waypoints;
        until([&]
              { return page::state()->host->waypoints->page(0, 5, empty_waypoints) == waypoint::Result::Ok; });
        click("New", page::state()->root);
        until([&]
              { return page::state()->view == page::View::WaypointName; });
        lv_textarea_set_text(page::state()->editor_widgets.title, "Camp waypoint");
        click("Save", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Editor; });
        const auto waypoint_id = page::state()->host->waypoints->commandResult().id;
        assert(waypoint_id && page::state()->draft.event.location_type == agenda::LocationType::Waypoint);
        page::runtime::request(page::Action::PickLocation);
        until([&]
              { return page::state()->view == page::View::LocationPicker; });
        click("Remove location", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(!(page::state()->draft.event.flags & agenda::HasLocation));
        page::runtime::request(page::Action::PickLocation);
        until([&]
              { return page::state()->view == page::View::LocationPicker; });
        click("Saved waypoint", page::state()->root);
        until([&]
              { return page::state()->view == page::View::WaypointPicker; });
        capture("agenda-waypoints.bmp");
        click("Camp waypoint", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(page::state()->draft.event.latitude_e7 == 225432100 && page::state()->draft.event.longitude_e7 == 1141234567);
        click("Save", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Agenda && label(page::state()->root, "New event"); });
        click("New event", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Detail; });
        const auto id = page::state()->draft.event.id;
        assert(id);
        click("Edit", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Editor; });
        assert(!std::strcmp(lv_textarea_get_text(page::state()->editor_widgets.note), "中文 😀 note"));
        lv_textarea_set_text(page::state()->editor_widgets.title, "Edited event");
        capture("agenda-editor.bmp");
        click("Save", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Agenda && label(page::state()->root, "Edited event"); });
        click("Edited event", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Detail; });
        assert(page::state()->draft.event.id == id);
        click("Delete", page::state()->root);
        until([&]
              { return page::state()->confirm_delete; });
        click("Delete", page::state()->root);
        until([&]
              { return page::state()->view == page::View::Agenda && !label(page::state()->root, "Edited event"); });
        agenda::EventRecord removed;
        assert(binding->source()->detail(id, removed) == agenda::AgendaResult::NotFound);
        page::runtime::request(page::Action::Back);
        until([&]
              { return ui_get_active_app() == nullptr && !ui_is_transition_pending(); });
        agenda::EventRecord reminder;
        reminder.state = agenda::RecordState::Active;
        reminder.start_time = clock.sample().calendar_seconds + 10;
        reminder.flags = agenda::HasReminder | agenda::HasLocation;
        reminder.location_type = agenda::LocationType::Coordinate;
        reminder.latitude_e7 = 311234567;
        reminder.longitude_e7 = 1212345678;
        std::strcpy(reminder.title, "Reminder target");
        assert(binding->source()->save(reminder).ok);
        until([&]
              { return binding->source()->commandResult().state != ui::agenda::CommandState::Pending; });
        {
            platform::audio::FocusScope focus(platform::audio::FocusOwner::Walkie, true);
            assert(focus);
            clock.advance(10);
            binding->source()->requestRefresh();
            until([&]
                  { return binding->source()->currentTime().calendar_seconds == clock.sample().calendar_seconds; });
            assert(!ui::agenda::reminder_popup::visible());
        }
        until([&]
              { return ui::agenda::reminder_popup::visible(); });
        assert(label(lv_layer_top(), "Reminder target"));
        capture("agenda-reminder.bmp");
        const auto guard = ticks();
        until([&]
              { return ticks() - guard >= 450; });
        click("Navigate", lv_layer_top());
        until([&]
              { return !ui::agenda::reminder_popup::visible() && ui_get_active_app() == binding->application() && !page::state(); });
        assert(lv_tileview_get_tile_active(main_screen) == lv_obj_get_child(main_screen, 1));
        assert(label(lv_obj_get_child(main_screen, 1), "Map"));
        capture("agenda-map.bmp");
        uint32_t back = LV_KEY_ESC;
        lv_group_send_data(lv_group_get_default(), back);
        until([&]
              { return page::state() && page::state()->view == page::View::Agenda; });
        ui_request_exit_to_menu(); // Exercise cancellation of a pending exit.
        binding->releaseUi();
        assert(!ui_get_active_app() && !ui_is_transition_pending() && !page::state());
        for (unsigned i = 0; i < 10; ++i)
        {
            lv_timer_handler();
            host.present();
        }
        platform::linux_runtime::shutdown_native_audio();
        facade.shutdown();
    }
    lv_deinit();
    binding.reset(); // Borrowed sources/descriptors outlive the menu's widgets.
    std::filesystem::remove_all(root);
}
