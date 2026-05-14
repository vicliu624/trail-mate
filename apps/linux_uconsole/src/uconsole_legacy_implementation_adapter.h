#pragma once

namespace trailmate
{
namespace apps
{
namespace linux_uconsole
{

struct UConsoleLegacyImplementationDescriptor
{
    const char* implementation_root = "apps/linux_uconsole";
    const char* app_shell = "apps/linux_uconsole_gtk";
    const char* target_id = "uconsole";
};

class UConsoleLegacyImplementationAdapter
{
  public:
    const UConsoleLegacyImplementationDescriptor& descriptor() const;
    bool validate() const;

  private:
    UConsoleLegacyImplementationDescriptor descriptor_{};
};

} // namespace linux_uconsole
} // namespace apps
} // namespace trailmate
