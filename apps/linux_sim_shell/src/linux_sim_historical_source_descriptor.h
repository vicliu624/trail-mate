#pragma once

namespace trailmate::apps::linux_sim_shell
{

struct LinuxSimHistoricalSourceDescriptor
{
    const char* historical_root_name = "removed root linux_sim";
    const char* historical_role = "pre-refactor Linux simulator implementation root";
    const char* replacement_owner = "apps/linux_sim_shell + modules/ui_ascii_runtime";
};

const LinuxSimHistoricalSourceDescriptor& linuxSimHistoricalSourceDescriptor();

} // namespace trailmate::apps::linux_sim_shell
