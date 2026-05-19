#pragma once

#include <cstddef>
#include <cstdint>

#include "phone/common/phone_result.h"

namespace phone
{

struct TimeSyncFact
{
    bool valid = false;
    std::uint64_t epoch_seconds = 0;
    bool synced = false;
};

struct LocationFixView
{
    bool valid = false;
    double lat = 0;
    double lon = 0;
    float altitude_m = 0;
    std::uint8_t satellites = 0;
};

struct DeviceStatusView
{
    bool lora_ready = false;
    bool gps_ready = false;
    bool ble_ready = false;
    bool time_synced = false;
};

struct ConfigSnapshotView
{
    std::uint8_t active_protocol = 0;
    std::uint8_t region = 0;
    std::uint8_t modem_preset = 0;
};

struct ConfigPatchView
{
    bool has_active_protocol = false;
    std::uint8_t active_protocol = 0;
    bool has_region = false;
    std::uint8_t region = 0;
    bool has_modem_preset = false;
    std::uint8_t modem_preset = 0;
};

enum class AppCommandKind : std::uint8_t
{
    None = 0,
    SendText,
    ApplyConfig,
    RequestConfig,
    RequestNodeInfo,
};

struct AppCommandView
{
    AppCommandKind kind = AppCommandKind::None;
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
};

class IPhoneAppFacade
{
  public:
    virtual ~IPhoneAppFacade() = default;

    virtual PhoneResult getTime(TimeSyncFact& out) = 0;
    virtual PhoneResult getLocation(LocationFixView& out) = 0;
    virtual PhoneResult getDeviceStatus(DeviceStatusView& out) = 0;
    virtual PhoneResult getConfig(ConfigSnapshotView& out) = 0;
    virtual PhoneResult applyConfigPatch(const ConfigPatchView& patch) = 0;
    virtual PhoneResult submitCommand(const AppCommandView& command) = 0;
};

} // namespace phone
