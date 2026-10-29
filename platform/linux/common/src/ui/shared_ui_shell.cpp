#include "ui/shared_ui_shell.h"

#include "ui/app_catalog_builder.h"
#include "ui/startup_ui_shell.h"
#include "ui/ui_boot.h"

namespace trailmate::cardputer_zero::linux_ui
{
namespace
{

[[nodiscard]] ui::startup_ui_shell::Hooks buildHooks(AppScreen* calendar, const ui::map::MapMarkerBinding* markers)
{
    // Select the compact product's pages while sharing their descriptors,
    // navigation hosts, and callbacks with the production catalog.
    ui::app_catalog_builder::FeatureFlags flags;
    flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
    flags.include_energy_sweep = true;
    flags.include_sstv = false;
    flags.include_walkie_talkie = true;
    flags.calendar_app = calendar;
    flags.map_markers = markers;
    ui::startup_ui_shell::Hooks hooks{};
    hooks.apps = ui::app_catalog_builder::build(flags);
    hooks.ux_pack_id = "cardputer_compact";
    return hooks;
}

constexpr unsigned int kMenuBuildDelayMs = 140U;
constexpr unsigned int kThemeReadyDelayMs = 280U;
constexpr unsigned int kFinalizeDelayMs = 420U;

} // namespace

bool SharedUiShellStartup::begin()
{
    if (phase_ != Phase::Idle)
    {
        return true;
    }

    started_at_ms_ = lv_tick_get();
    if (!ui::startup_ui_shell::beginBootUi(buildHooks(calendar_, markers_), false, "Loading language packs..."))
    {
        return false;
    }
    ui::startup_ui_shell::prepareBootResources();

    phase_ = Phase::BootVisible;
    return true;
}

bool SharedUiShellStartup::runPhase(Phase next_phase, const char* log_line)
{
    if (log_line != nullptr && log_line[0] != '\0')
    {
        ui::boot::set_log_line(log_line);
    }
    phase_ = next_phase;
    return true;
}

bool SharedUiShellStartup::tick()
{
    if (phase_ == Phase::Idle)
    {
        return begin();
    }

    if (phase_ == Phase::Finalized)
    {
        return true;
    }

    const unsigned int elapsed = lv_tick_elaps(started_at_ms_);
    if (phase_ == Phase::BootVisible && elapsed >= kMenuBuildDelayMs)
    {
        ui::boot::set_log_line("Building menu shell...");
        if (!ui::startup_ui_shell::initializeMenuSkeleton(buildHooks(calendar_, markers_)))
        {
            return false;
        }
        return runPhase(Phase::MenuBuilt, nullptr);
    }

    if (phase_ == Phase::MenuBuilt && elapsed >= kThemeReadyDelayMs)
    {
        return runPhase(Phase::ThemeReady, "Preparing navigation and apps...");
    }

    if (phase_ == Phase::ThemeReady && elapsed >= kFinalizeDelayMs)
    {
        ui::boot::set_log_line("Starting Trail Mate...");
        if (!ui::startup_ui_shell::finalizeStartup(buildHooks(calendar_, markers_), false))
        {
            return false;
        }
        return runPhase(Phase::Finalized, nullptr);
    }

    return true;
}

bool SharedUiShellStartup::ready() const noexcept
{
    return phase_ == Phase::Finalized;
}

} // namespace trailmate::cardputer_zero::linux_ui
