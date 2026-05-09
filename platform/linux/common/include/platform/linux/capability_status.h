#pragma once

// Re-export the shared contract types into the Linux runtime namespace.
#include "platform/ui/capability_status.h"

namespace platform::linux_runtime
{

using CapabilityState = ::platform::ui::CapabilityState;
using CapabilityStatus = ::platform::ui::CapabilityStatus;

inline const char* capability_state_label(::platform::ui::CapabilityState state)
{
    return ::platform::ui::capability_state_label(state);
}

} // namespace platform::linux_runtime
