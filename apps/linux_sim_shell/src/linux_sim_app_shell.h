#pragma once

namespace trailmate
{
namespace apps
{
namespace linux_sim_shell
{

struct LinuxSimAppShellConfig
{
    const char* target_id = "linux_sim";
    const char* ux_pack_id = "simulator_full";
    const char* transitional_source = "apps/linux_sim";
    const char* legacy_adapter_target = "trailmate_linux_sim_legacy_adapter";
};

class LinuxSimAppShell
{
  public:
    const LinuxSimAppShellConfig& config() const;
    bool validate() const;

  private:
    LinuxSimAppShellConfig config_{};
};

} // namespace linux_sim_shell
} // namespace apps
} // namespace trailmate
