#pragma once

#include "gps/usecase/gps_runtime_config.h"

#include <cstdint>

namespace gps
{

enum class ConfigApplyFailure : uint8_t
{
    None = 0,
    Unsupported,
    DeviceNotReady,
    IoError,
};

struct ConfigApplyResult
{
    bool ok = false;
    ConfigApplyFailure failure = ConfigApplyFailure::None;

    static ConfigApplyResult success()
    {
        return ConfigApplyResult{true, ConfigApplyFailure::None};
    }

    static ConfigApplyResult fail(ConfigApplyFailure failure)
    {
        return ConfigApplyResult{false, failure};
    }
};

class IGpsConfigApplier
{
  public:
    virtual ~IGpsConfigApplier() = default;

    virtual ConfigApplyResult apply(const GnssRuntimeConfig& config) = 0;
};

} // namespace gps
