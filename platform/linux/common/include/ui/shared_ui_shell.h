#pragma once

namespace trailmate::cardputer_zero::linux_ui
{

class SharedUiShellStartup
{
  public:
    SharedUiShellStartup() = default;

    bool begin();
    bool tick();

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
};

} // namespace trailmate::cardputer_zero::linux_ui
