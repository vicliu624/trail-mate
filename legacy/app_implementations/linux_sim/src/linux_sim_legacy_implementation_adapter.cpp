#include "linux_sim_legacy_implementation_adapter.h"

namespace trailmate
{
namespace apps
{
namespace linux_sim
{

const LinuxSimLegacyImplementationDescriptor&
LinuxSimLegacyImplementationAdapter::descriptor() const
{
    return descriptor_;
}

bool LinuxSimLegacyImplementationAdapter::validate() const
{
    return descriptor_.implementation_root != nullptr &&
           descriptor_.app_shell != nullptr &&
           descriptor_.target_id != nullptr;
}

} // namespace linux_sim
} // namespace apps
} // namespace trailmate
