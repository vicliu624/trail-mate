/**
 * @file mt_region.h
 * @brief Meshtastic region utilities
 */

#pragma once

#include "generated/meshtastic/config.pb.h"
#include <cstdint>

namespace chat
{
namespace meshtastic
{

struct RegionInfo
{
    meshtastic_Config_LoRaConfig_RegionCode code;
    const char* label;
    float freq_start_mhz;
    float freq_end_mhz;
    float duty_cycle_percent;
    float spacing_khz;
    uint8_t power_limit_dbm; // 0 means unspecified
    bool audio_permitted;
    bool frequency_switching;
    bool wide_lora;
};

const RegionInfo* getRegionTable(size_t* out_count);
const RegionInfo* findRegion(meshtastic_Config_LoRaConfig_RegionCode code);
const char* presetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset);
float computeFrequencyMhz(const RegionInfo* region, float bw_khz, const char* channel_name);
float estimateFrequencyMhz(uint8_t region_code, uint8_t modem_preset);

} // namespace meshtastic
} // namespace chat
