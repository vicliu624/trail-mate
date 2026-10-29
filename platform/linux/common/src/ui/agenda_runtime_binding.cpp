#include "ui/agenda_runtime_binding.h"
#include "platform/linux/waypoint_source.h"
#include "platform/audio/audio_focus.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/chat_voice_runtime.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/screens/agenda/agenda_page_flow.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_reminder_popup.h"
#include "ui_presentation/agenda/agenda_location_source.h"
#include <cstdlib>

namespace ui::assets
{
extern "C" const lv_image_dsc_t agenda;
}
namespace trailmate::linux_ui
{
struct AgendaRuntimeBinding::Impl
{
    ::platform::linux_runtime::LinuxWaypointSource waypoints;
    ::platform::linux_runtime::LinuxAgendaSource source;
    ::ui::agenda::GpsAgendaLocationSource locations{::ui::presentation_sources::runtime_gps_status_source()};
    ::ui::agenda::page::Host host;
    ::ui::agenda::page::Flow flow{host};
    ::ui::map::MapMarkerBinding marker_binding{};
    ::ui::CallbackAppScreen app{"calendar", "Agenda", &::ui::assets::agenda,
                                ::ui::agenda::page::Flow::enter, ::ui::agenda::page::Flow::exit, &flow};
    uint32_t revision = 0, sounded_revision = 0;
    bool was_ready = false, released = false;
    explicit Impl(::platform::linux_runtime::LinuxAgendaOptions options) : waypoints(options.database), source(std::move(options))
    {
        host.source = &source;
        host.actions = &source;
        host.locations = &locations;
        host.waypoints = &waypoints;
        host.waypoint_actions = &waypoints;
        host.navigation.request_exit = [](void*)
        { ui_request_exit_to_menu(); };
        marker_binding.source = &source;
        marker_binding.allocate = [](size_t bytes)
        { return std::malloc(bytes); };
        marker_binding.release = [](void* memory)
        { std::free(memory); };
        ::ui::menu::dashboard::bindAgendaSource(&source);
    }
};
AgendaRuntimeBinding::AgendaRuntimeBinding(::platform::linux_runtime::LinuxAgendaOptions options)
{
#if defined(__linux__)
    impl_ = std::make_unique<Impl>(std::move(options));
#else
    (void)options;
#endif
}
AgendaRuntimeBinding::~AgendaRuntimeBinding() { releaseUi(); }
AppScreen* AgendaRuntimeBinding::application() { return impl_ && !impl_->released ? &impl_->app : nullptr; }
const ::ui::map::MapMarkerBinding* AgendaRuntimeBinding::markers() { return impl_ && !impl_->released ? &impl_->marker_binding : nullptr; }
::platform::linux_runtime::LinuxAgendaSource* AgendaRuntimeBinding::source() { return impl_ ? &impl_->source : nullptr; }
void AgendaRuntimeBinding::tick()
{
    if (!impl_ || impl_->released) return;
    auto& root = *impl_;
    namespace popup = ::ui::agenda::reminder_popup;
    const auto status = root.source.markerStatus();
    if (root.was_ready && !status.ready)
    {
        popup::close();
        root.flow.invalidateStorage();
    }
    root.was_ready = status.ready;
    if (status.revision != root.revision)
    {
        root.revision = status.revision;
        ::ui::agenda::page::runtime::refresh();
    }
    if (!ui_is_interruption_app_active() && !ui_is_transition_pending() && ui_get_active_app() != &root.app)
        root.flow.discardSuspended();
    if (root.flow.needsActivation() && main_screen && !popup::visible() &&
        !ui_is_overlay_active() && !ui_is_transition_pending() && !ui_is_interruption_app_active())
        (void)ui_open_app(&root.app);
    root.flow.tick();
    const bool allowed = !root.flow.mapActive() && popup::canPresent() &&
                         ::ui::agenda::page::canPresentReminder() && !::ui::chat_voice::isOutboundActive() &&
                         (popup::visible() || !::platform::audio::music_blocked());
    root.source.setPresentationAllowed(allowed && !popup::visible());
    popup::Host host{&root.source, &root.source, &root.source, ::platform::ui::screen::wake_for_modal};
    host.navigation_context = &root.flow;
    host.prepare_navigation = [](void* context, int32_t latitude, int32_t longitude) -> const char*
    {
        if (!main_screen) return "Map is unavailable on this target.";
        return static_cast<::ui::agenda::page::Flow*>(context)->prepareDestination(latitude, longitude);
    };
    host.finish_navigation = [](void* context, bool accepted)
    { static_cast<::ui::agenda::page::Flow*>(context)->finishDestination(accepted); };
    popup::tick(host, allowed);
    const auto reminder = root.source.reminderSnapshot();
    if (popup::visible() && reminder.reminder.valid && reminder.revision != root.sounded_revision)
    {
        root.sounded_revision = reminder.revision;
        ::platform::ui::device::play_message_tone();
    }
}
void AgendaRuntimeBinding::releaseUi()
{
    if (!impl_ || impl_->released) return;
    auto& root = *impl_;
    root.released = true;
    ::ui::agenda::reminder_popup::close();
    ui_detach_app(&root.app);
    ::ui::agenda::page::Flow::exit(&root.flow, nullptr);
    root.flow.discardSuspended();
    ::ui::menu::dashboard::bindAgendaSource(nullptr);
    root.source.setPresentationAllowed(false);
    root.source.shutdown();
    root.waypoints.shutdown();
}
} // namespace trailmate::linux_ui
