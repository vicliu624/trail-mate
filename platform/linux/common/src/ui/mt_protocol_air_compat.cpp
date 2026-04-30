#include "meshtastic/config.pb.h"

#include <cstdint>

namespace chat
{
namespace meshtastic
{

void modemPresetToParams(meshtastic_Config_LoRaConfig_ModemPreset preset, bool wide_lora,
                         float& bw_khz, uint8_t& sf, uint8_t& cr_denom)
{
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bw_khz = wide_lora ? 1625.0f : 500.0f;
        cr_denom = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        bw_khz = wide_lora ? 1625.0f : 500.0f;
        cr_denom = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bw_khz = wide_lora ? 406.25f : 125.0f;
        cr_denom = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bw_khz = wide_lora ? 406.25f : 125.0f;
        cr_denom = 8;
        sf = 12;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 11;
        break;
    }
}

} // namespace meshtastic
} // namespace chat
