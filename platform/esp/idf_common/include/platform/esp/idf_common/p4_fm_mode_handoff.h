#pragma once
#include "lr2021_iq.h"
#include "platform/shared/radio/fm_iq_stream.h"
#include <array>

namespace platform::esp::idf_common::detail
{
struct GfskConfig
{
    float frequency = 0, bitrate = 0, deviation = 0, bandwidth = 0;
    int8_t power = 0;
    uint16_t preamble = 0;
    std::array<uint8_t, 8> sync{};
    size_t sync_size = 0;
    uint8_t crc = 0;
};

// Caller holds the physical mutex and has stopped all other radio users.
// The legacy engine receives an actual LoRa configuration, never a relabeled
// FSK snapshot. Retain the committed FSK configuration until restoration ends.
template <typename State, typename Capture>
bool withFmPacketMode(State& state, platform::shared::radio::FmIqResult& result, Capture capture)
{
    if (!state.fsk) return capture();
    if (!state.fsk_profile_valid)
    {
        result.capture_error = LR2021_IQ_STATE;
        return state.fail("FM requires a committed FSK profile for restoration");
    }
    const bool receiving = state.receiving;
    const bool prepared = state.prepareFmPacketMode();
    const bool captured = prepared && capture();
    if (!prepared) result.capture_error = LR2021_IQ_IO;
    if (!state.restoreFsk(receiving))
    {
        result.recovery_error = LR2021_IQ_IO;
        return false;
    }
    return captured || state.fail("FM capture failed; prior FSK restored");
}
} // namespace platform::esp::idf_common::detail
