#pragma once

namespace trailmate::apps::linux_sim_shell
{

struct LinuxSimLegacySourceDescriptor
{
    const char* root_path = "legacy/app_implementations/linux_sim";
    const char* historical_name = "linux_sim";
    const char* replacement_owner = "apps/linux_sim_shell";
    bool compatibility_required = true;
};

const LinuxSimLegacySourceDescriptor& linuxSimLegacySourceDescriptor();

} // namespace trailmate::apps::linux_sim_shell
