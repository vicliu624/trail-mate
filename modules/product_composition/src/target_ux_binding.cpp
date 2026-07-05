#include "product_composition/target_ux_binding.h"

#include <cstring>

namespace product_composition
{
namespace
{

constexpr TargetUxBinding kTargetUxBindings[] = {
    {"tab5", "tab5_touch", "compatibility", "compatibility", false},
    {"t_display_p4_tft", "t_display_p4_touch", "t_display_p4_touch", nullptr, true},
    {"t_display_p4_amoled", "t_display_p4_touch", "t_display_p4_touch", nullptr, true},
    {"tlora_pager", "pager_compact", "compatibility", "compatibility", false},
    {"tdeck", "deck_full", "compatibility", "compatibility", false},
    {"twatch", "watch_compact", "compatibility", "compatibility", false},
    {"uconsole", "uconsole_desktop", "uconsole_desktop", nullptr, true},
    {"linux_sim", "simulator_full", "simulator_full", nullptr, true},
    {"cardputerzero", "cardputer_compact", "cardputer_compact", nullptr, true},
    {"gat562_mesh_evb_pro", "node_headless", "tiny_node_status", "tiny_node_status", false},
    {"t-echo-lite", "node_headless", "tiny_node_status", "tiny_node_status", false},
    {"t-impulse-plus", "node_headless", "tiny_node_status_64x32", "tiny_node_status", false},
};

} // namespace

const TargetUxBinding* findTargetUxBinding(const char* target_id)
{
    if (target_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& binding : kTargetUxBindings)
    {
        if (std::strcmp(binding.target_id, target_id) == 0)
        {
            return &binding;
        }
    }
    return nullptr;
}

const TargetUxBinding* allTargetUxBindings(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kTargetUxBindings) / sizeof(kTargetUxBindings[0]);
    }
    return kTargetUxBindings;
}

} // namespace product_composition
