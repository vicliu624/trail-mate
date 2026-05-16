#pragma once

#include <cstddef>

namespace product_composition
{

struct TargetUxBinding
{
    const char* target_id;
    const char* desired_ux_pack_id;
    const char* active_ux_pack_id;
    const char* fallback_ux_pack_id;
    bool final_ux_pack_available;
};

const TargetUxBinding* findTargetUxBinding(const char* target_id);
const TargetUxBinding* allTargetUxBindings(std::size_t* count);

} // namespace product_composition
