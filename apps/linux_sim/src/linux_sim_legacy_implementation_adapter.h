#pragma once

namespace trailmate
{
namespace apps
{
namespace linux_sim
{

struct LinuxSimLegacyImplementationDescriptor
{
    const char* implementation_root = "apps/linux_sim";
    const char* app_shell = "apps/linux_sim_shell";
    const char* target_id = "linux_sim";
};

class LinuxSimLegacyImplementationAdapter
{
  public:
    const LinuxSimLegacyImplementationDescriptor& descriptor() const;
    bool validate() const;

  private:
    LinuxSimLegacyImplementationDescriptor descriptor_{};
};

} // namespace linux_sim
} // namespace apps
} // namespace trailmate
