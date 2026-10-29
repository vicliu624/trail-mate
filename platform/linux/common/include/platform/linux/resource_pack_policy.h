#pragma once
#include "platform/ui/pack_repository_runtime.h"
#include <string>
#include <vector>

namespace platform::linux_runtime
{
struct ResourcePackPolicy
{
    std::string firmware_version;
    std::string memory_profile;
};
// Configure at the composition boundary before using the repository; immutable thereafter.
bool configure_resource_pack_policy(const ResourcePackPolicy& policy);
ResourcePackPolicy resource_pack_policy();
void evaluate_resource_pack_compatibility(::ui::runtime::packs::PackageRecord& package, const ResourcePackPolicy& policy);
std::vector<std::string> resource_memory_profiles(const std::string& csv);
} // namespace platform::linux_runtime
