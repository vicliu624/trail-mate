#include "uconsole_legacy_implementation_adapter.h"

namespace trailmate
{
namespace apps
{
namespace linux_uconsole
{

const UConsoleLegacyImplementationDescriptor&
UConsoleLegacyImplementationAdapter::descriptor() const
{
    return descriptor_;
}

bool UConsoleLegacyImplementationAdapter::validate() const
{
    return descriptor_.implementation_root != nullptr &&
           descriptor_.app_shell != nullptr &&
           descriptor_.target_id != nullptr;
}

} // namespace linux_uconsole
} // namespace apps
} // namespace trailmate
