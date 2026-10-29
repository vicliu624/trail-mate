#pragma once
#include "ui/app_catalog_builder.h"

namespace trailmate::cardputer_zero::linux_ui
{

class SharedUiShellStartup
{
  public:
    SharedUiShellStartup() = default;

    bool begin();
    bool tick();
    void setAgenda(AppScreen* app, const ::ui::map::MapMarkerBinding* markers)
    { calendar_ = app; markers_ = markers; }

    [[nodiscard]] bool ready() const noexcept;

  private:
    enum class Phase
    {
        Idle,
        BootVisible,
        MenuBuilt,
        ThemeReady,
        Finalized,
    };

    [[nodiscard]] bool runPhase(Phase next_phase, const char* log_line);

    Phase phase_{Phase::Idle};
    unsigned int started_at_ms_{0};
    AppScreen* calendar_ = nullptr;
    const ::ui::map::MapMarkerBinding* markers_ = nullptr;
};

} // namespace trailmate::cardputer_zero::linux_ui
