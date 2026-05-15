#include "esp_idf_legacy_implementation_adapter.h"

namespace trailmate
{
namespace apps
{
namespace esp_idf
{

const EspIdfLegacyImplementationDescriptor&
EspIdfLegacyImplementationAdapter::descriptor() const
{
    return descriptor_;
}

bool EspIdfLegacyImplementationAdapter::validate() const
{
    return descriptor_.implementation_root != nullptr &&
           descriptor_.app_shell != nullptr &&
           descriptor_.build_wrapper != nullptr;
}

} // namespace esp_idf
} // namespace apps
} // namespace trailmate
