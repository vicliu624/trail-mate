/**
 * @file mt_region.cpp
 * @brief Meshtastic region utilities
 */

#include "mt_region.h"
#include <cmath>

namespace chat
{
namespace meshtastic
{

namespace
{

const RegionInfo kRegions[] = {
    {meshtastic_Config_LoRaConfig_RegionCode_UNSET, "UNSET", 902.0f, 928.0f, 100.0f, 0.0f, 30, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_US, "US", 902.0f, 928.0f, 100.0f, 0.0f, 30, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_433, "EU_433", 433.0f, 434.0f, 10.0f, 0.0f, 10, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_868, "EU_868", 869.4f, 869.65f, 10.0f, 0.0f, 27, false, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_CN, "CN", 470.0f, 510.0f, 100.0f, 0.0f, 19, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_JP, "JP", 920.5f, 923.5f, 100.0f, 0.0f, 13, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ, "ANZ", 915.0f, 928.0f, 100.0f, 0.0f, 30, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_KR, "KR", 920.0f, 923.0f, 100.0f, 0.0f, 23, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_TW, "TW", 920.0f, 925.0f, 100.0f, 0.0f, 27, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_RU, "RU", 868.7f, 869.2f, 100.0f, 0.0f, 20, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_IN, "IN", 865.0f, 867.0f, 100.0f, 0.0f, 30, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_NZ_865, "NZ_865", 864.0f, 868.0f, 100.0f, 0.0f, 36, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_TH, "TH", 920.0f, 925.0f, 100.0f, 0.0f, 16, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_LORA_24, "LORA_24", 2400.0f, 2483.5f, 100.0f, 0.0f, 10, true, false, true},
    {meshtastic_Config_LoRaConfig_RegionCode_UA_433, "UA_433", 433.0f, 434.7f, 10.0f, 0.0f, 10, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_UA_868, "UA_868", 868.0f, 868.6f, 1.0f, 0.0f, 14, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_433, "MY_433", 433.0f, 435.0f, 100.0f, 0.0f, 20, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_919, "MY_919", 919.0f, 924.0f, 100.0f, 0.0f, 27, true, true, false},
    {meshtastic_Config_LoRaConfig_RegionCode_SG_923, "SG_923", 917.0f, 925.0f, 100.0f, 0.0f, 20, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_433, "PH_433", 433.0f, 434.7f, 100.0f, 0.0f, 10, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_868, "PH_868", 868.0f, 869.4f, 100.0f, 0.0f, 14, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_915, "PH_915", 915.0f, 918.0f, 100.0f, 0.0f, 24, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ_433, "ANZ_433", 433.05f, 434.79f, 100.0f, 0.0f, 14, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_KZ_433, "KZ_433", 433.075f, 434.775f, 100.0f, 0.0f, 10, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_KZ_863, "KZ_863", 863.0f, 868.0f, 100.0f, 0.0f, 30, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_NP_865, "NP_865", 865.0f, 868.0f, 100.0f, 0.0f, 30, true, false, false},
    {meshtastic_Config_LoRaConfig_RegionCode_BR_902, "BR_902", 902.0f, 907.5f, 100.0f, 0.0f, 30, true, false, false},
};
uint32_t djb2Hash(const char* str)
{
    uint32_t hash = 5381;
    int c;
    while ((c = *str++) != 0)
    {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }
    return hash;
}

} // namespace

const RegionInfo* getRegionTable(size_t* out_count)
{
    if (out_count)
    {
        *out_count = sizeof(kRegions) / sizeof(kRegions[0]);
    }
    return kRegions;
}

const RegionInfo* findRegion(meshtastic_Config_LoRaConfig_RegionCode code)
{
    for (const auto& region : kRegions)
    {
        if (region.code == code)
        {
            return &region;
        }
    }
    return &kRegions[0];
}

const char* presetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
        return "LongFast";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        return "LongModerate";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        return "LongSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
        return "VeryLongSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        return "MediumSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        return "MediumFast";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        return "ShortSlow";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        return "ShortFast";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        return "ShortTurbo";
    default:
        return "LongFast";
    }
}

float computeFrequencyMhz(const RegionInfo* region, float bw_khz, const char* channel_name)
{
    if (!region || !channel_name)
    {
        return 0.0f;
    }
    float spacing_khz = region->spacing_khz > 0.0f ? region->spacing_khz : 0.0f;
    float spacing_mhz = spacing_khz / 1000.0f;
    float bw_mhz = bw_khz / 1000.0f;
    float span_mhz = region->freq_end_mhz - region->freq_start_mhz;
    uint32_t num_channels = static_cast<uint32_t>(floor(span_mhz / (spacing_mhz + bw_mhz)));
    if (num_channels < 1)
    {
        num_channels = 1;
    }
    uint32_t channel_num = djb2Hash(channel_name) % num_channels;
    return region->freq_start_mhz + (bw_khz / 2000.0f) + (channel_num * (bw_khz / 1000.0f));
}

float estimateFrequencyMhz(uint8_t region_code, uint8_t modem_preset)
{
    meshtastic_Config_LoRaConfig_RegionCode region =
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(region_code);
    if (region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }
    const RegionInfo* info = findRegion(region);
    meshtastic_Config_LoRaConfig_ModemPreset preset =
        static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(modem_preset);

    float bw_khz = 250.0f;
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bw_khz = info->wide_lora ? 1625.0f : 500.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bw_khz = info->wide_lora ? 812.5f : 250.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = info->wide_lora ? 812.5f : 250.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = info->wide_lora ? 812.5f : 250.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = info->wide_lora ? 812.5f : 250.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bw_khz = info->wide_lora ? 406.25f : 125.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
        bw_khz = info->wide_lora ? 406.25f : 125.0f;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        bw_khz = info->wide_lora ? 812.5f : 250.0f;
        break;
    }

    const char* channel_name = presetDisplayName(preset);
    return computeFrequencyMhz(info, bw_khz, channel_name);
}

} // namespace meshtastic
} // namespace chat
