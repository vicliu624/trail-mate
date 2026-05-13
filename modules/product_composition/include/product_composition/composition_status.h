#pragma once

namespace product_composition
{

enum class CompositionStatus
{
    Ok,
    MissingRequiredService,
    MissingRequiredCapability,
    UnsupportedTarget,
    InvalidManifest,
    RuntimeUnavailable,
};

} // namespace product_composition
