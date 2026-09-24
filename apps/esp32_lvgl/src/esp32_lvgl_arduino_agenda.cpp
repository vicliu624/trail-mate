#include "esp32_lvgl_arduino_agenda.h"

#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
#include "esp32_lvgl_runtime_config.h"
#include "platform/esp/arduino_common/agenda_clock.h"
#include "platform/esp/arduino_common/storage/agenda_storage.h"
#include "platform/esp/arduino_common/storage/sd_record_file_io.h"
#include "platform/esp/arduino_common/storage/waypoint_storage.h"
#include "platform/esp/common/storage/agenda_file_store.h"
#include "platform/esp/common/storage/waypoint_file_store.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "product_composition/agenda_composition.h"
#include "product_composition/agenda_target.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/screens/agenda/agenda_page_flow.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_page_shell.h"
#include "ui/screens/agenda/agenda_reminder_popup.h"
#include "ui_lvgl_ux_packs/packs/manifest_compatibility_ux_pack.h"
#include "ui_presentation/waypoint/waypoint_model.h"
#include <new>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#else
#include <cstdlib>
#endif

namespace ui::assets
{
// Keep the C asset's existing name without colliding with namespace agenda.
extern "C" const lv_image_dsc_t agenda;
} // namespace ui::assets

namespace trailmate::apps::esp32_lvgl::arduino_agenda
{
namespace
{
namespace storage = platform::esp::arduino_common::storage;

struct WaypointSession
{
    storage::SdRecordFileIo files;
    platform::esp::storage::WaypointFileStore store{storage::kWaypointFile, storage::kWaypointInitializationFile, files};
    ui::waypoint::Model model{store};
    explicit WaypointSession(bool ready)
    {
        files.bindSession(storage::agenda_storage_session());
        model.setReady(ready && store.begin() == ::waypoint::Result::Ok);
    }
};
static_assert(sizeof(WaypointSession) < 256, "Waypoint session storage must remain bounded");

struct Root
{
    storage::SdRecordFileIo files;
    platform::esp::storage::AgendaFileStore store{storage::kAgendaFile, storage::kAgendaInitializationFile, files};
    platform::esp::arduino_common::AgendaClock clock;
    product_composition::AgendaComposition composition{store, clock};
    ui::agenda::GpsAgendaLocationSource locations{ui::presentation_sources::runtime_gps_status_source()};
    ui::agenda::page::Host page;
    ui::agenda::page::Flow flow{page};
    ui::map::MapMarkerBinding markers{};
    WaypointSession* waypoint_session = nullptr;
    ui::CallbackAppScreen app{"calendar", "Agenda", &ui::assets::agenda,
                              enter, exit, this};
    bool initialized = false;
    bool storage_ready = false;
    uint32_t storage_session = 0;
    uint32_t sounded_reminder_revision = 0;
    uint64_t last_storage_attempt = 0;

    void updateStorage(bool first = false)
    {
        const bool available = storage::agenda_storage_available();
        const auto session = storage::agenda_storage_session();
        const bool changed = session != storage_session;
        storage_session = session;
        if (!available || changed)
        {
            if (storage_ready)
            {
                storage_ready = false;
                composition.setStorageReady(false);
                if (waypoint_session) waypoint_session->model.setReady(false);
                ui::agenda::reminder_popup::close();
                flow.invalidateStorage();
            }
            if (!available) return;
        }
        if (storage_ready) return;
        const auto now = clock.sample().monotonic_seconds;
        if (!first && !changed && now == last_storage_attempt) return;
        last_storage_attempt = now;
        files.bindSession(storage_session);
        storage_ready = storage::prepare_agenda_storage() && store.begin() == ::agenda::StoreResult::Ok;
        composition.setStorageReady(storage_ready);
        if (storage_ready)
        {
            if (waypoint_session)
            {
                waypoint_session->files.bindSession(storage_session);
                waypoint_session->model.setReady(waypoint_session->store.begin() == ::waypoint::Result::Ok);
            }
            ui::agenda::page::runtime::refresh();
        }
    }

    static void enter(void* context, lv_obj_t* parent)
    {
        auto& self = *static_cast<Root*>(context);
        self.updateStorage();
        if (!self.waypoint_session) self.waypoint_session = new (std::nothrow) WaypointSession(self.storage_ready);
        if (self.waypoint_session)
        {
            self.page.waypoints = &self.waypoint_session->model;
            self.page.waypoint_actions = &self.waypoint_session->model;
        }
        ui::agenda::page::Flow::enter(&self.flow, parent);
    }
    void releaseWaypoints()
    {
        updateStorage();
        // An accepted operation outlives its widgets and completes exactly once.
        if (waypoint_session) waypoint_session->model.pump();
        page.waypoints = nullptr;
        page.waypoint_actions = nullptr;
        delete waypoint_session;
        waypoint_session = nullptr;
    }
    static void exit(void* context, lv_obj_t* parent)
    {
        auto& self = *static_cast<Root*>(context);
        ui::agenda::page::Flow::exit(&self.flow, parent);
        if (!ui_is_interruption_app_active()) self.releaseWaypoints();
    }

    Root()
    {
        // Bind the existing presentation graph without retaining another graph
        // in idle RAM. The contracts and their owner outlive the page.
        ui::workspace::PresentationWorkspace workspace;
        composition.bind(workspace);
        markers.source = workspace.agenda;
        markers.allocate = [](std::size_t bytes) -> void*
        {
#if defined(ESP_PLATFORM)
            return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            return std::malloc(bytes);
#endif
        };
        markers.release = [](void* memory)
        {
#if defined(ESP_PLATFORM)
            heap_caps_free(memory);
#else
            std::free(memory);
#endif
        };
        page.source = workspace.agenda;
        page.actions = workspace.agenda;
        page.locations = &locations;
        page.navigation.request_exit = [](void*)
        { ui_request_exit_to_menu(); };
    }
};

Root root;
// Charge both new registry objects and their ESP C++ local-static guards as
// well as the renderer's idle State*. LVGL objects exist only while open.
constexpr std::size_t kRegistrationBytes = 2 * (sizeof(ui_lvgl_ux::ManifestCompatibilityUxPack) + sizeof(uint64_t));
// Selection extends the existing shared Map model. Charge its bounded result
// and a full alignment unit for the previous-tool field instead of hiding the
// feature's cost outside the Agenda root.
constexpr std::size_t kMapSelectionBytes = sizeof(ui::map::MapLocationSelection) + alignof(ui::map::MapLocationSelection) + 4 * sizeof(void*);
// Native LVGL tests compile this composition on a 64-bit host. Their pointer
// and callback sizes must not relax the 1 KiB budget enforced on ESP targets.
static_assert(sizeof(Root) + 2 * sizeof(void*) + kRegistrationBytes + kMapSelectionBytes < (sizeof(void*) == 4 ? 1024 : 1280),
              "Agenda root and shared selection state exceeded the idle RAM budget");
} // namespace

void initialize()
{
    if (root.initialized || !product_composition::targetHasAgenda(esp32LvglRuntimeTargetProfile())) return;
    root.updateStorage(true);
    // Keep the page available to report storage failure, never silently format.
    root.initialized = true;
}

AppScreen* application()
{
    return root.initialized ? &root.app : nullptr;
}

const ui::map::MapMarkerBinding* mapMarkers()
{
    return root.initialized ? &root.markers : nullptr;
}

void tick()
{
    if (!root.initialized) return;
    root.updateStorage();
    if (root.waypoint_session) root.waypoint_session->model.pump();
    namespace popup = ui::agenda::reminder_popup;
    if (!ui_is_interruption_app_active() && !ui_is_transition_pending() && ui_get_active_app() != &root.app)
    {
        root.flow.discardSuspended();
        root.releaseWaypoints();
    }
    if (root.flow.needsActivation() && main_screen && !popup::visible() &&
        !ui_is_overlay_active() && !ui_is_transition_pending() && !ui_is_interruption_app_active())
        ui_switch_to_app(&root.app, main_screen);
    root.flow.tick();
    const bool may_present = !root.flow.mapActive() && popup::canPresent() && ui::agenda::page::canPresentReminder();
    // Do not replace the command result until the editor has observed its save.
    // A visible popup also finishes/withdraws before another reminder is sent.
    root.composition.tick(may_present && !popup::visible());
    ui::workspace::PresentationWorkspace workspace;
    root.composition.bind(workspace);
    popup::Host host{workspace.agenda, workspace.agenda, workspace.agenda_reminders,
                     platform::ui::screen::wake_for_modal};
    host.navigation_context = &root.flow;
    host.prepare_navigation = [](void* context, int32_t latitude, int32_t longitude) -> const char*
    {
        if (!main_screen) return "Map is unavailable on this target.";
        return static_cast<ui::agenda::page::Flow*>(context)->prepareDestination(latitude, longitude);
    };
    host.finish_navigation = [](void* context, bool accepted)
    { static_cast<ui::agenda::page::Flow*>(context)->finishDestination(accepted); };
    popup::tick(host, may_present);
    const auto reminder = workspace.agenda_reminders->reminderSnapshot();
    if (popup::visible() && reminder.reminder.valid &&
        reminder.revision != root.sounded_reminder_revision)
    {
        root.sounded_reminder_revision = reminder.revision;
        platform::ui::device::play_message_tone();
    }
}
} // namespace trailmate::apps::esp32_lvgl::arduino_agenda
#else
namespace trailmate::apps::esp32_lvgl::arduino_agenda
{
void initialize() {}
AppScreen* application() { return nullptr; }
const ui::map::MapMarkerBinding* mapMarkers() { return nullptr; }
void tick() {}
} // namespace trailmate::apps::esp32_lvgl::arduino_agenda
#endif
