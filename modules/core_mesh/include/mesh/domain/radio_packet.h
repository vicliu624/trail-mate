#pragma once

#include "mesh/domain/mesh_packet.h"

#include <stddef.h>
#include <stdint.h>

namespace mesh
{

struct RadioConfig
{
    uint32_t frequency_hz = 0;
    uint32_t bandwidth_hz = 0;
    uint8_t spreading_factor = 0;
    uint8_t coding_rate = 0;
    uint8_t sync_word = 0;
    int8_t tx_power_dbm = 0;
    bool dio2_as_rf_switch = false;
    float dio3_tcxo_voltage = 0.0f;
};

struct RadioRxPacket
{
    uint8_t bytes[256]{};
    size_t size = 0;
    int16_t rssi = 0;
    int8_t snr = 0;
    uint32_t received_at_ms = 0;
};

enum class RadioFailure : uint8_t
{
    None = 0,
    NotReady,
    InvalidConfig,
    TxFailed,
    RxFailed,
    Timeout,
    Busy,
    Unsupported,
};

struct RadioResult
{
    bool ok = false;
    RadioFailure failure = RadioFailure::None;

    RadioResult() = default;

    RadioResult(bool result_ok, RadioFailure result_failure)
        : ok(result_ok),
          failure(result_failure)
    {
    }

    static RadioResult success()
    {
        return RadioResult(true, RadioFailure::None);
    }

    static RadioResult fail(RadioFailure failure)
    {
        return RadioResult(false, failure);
    }
};

} // namespace mesh
