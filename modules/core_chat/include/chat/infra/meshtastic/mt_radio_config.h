#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/meshtastic/mt_region.h"

#include <cstdint>

namespace chat
{
namespace meshtastic
{

constexpr std::uint8_t kMeshtasticLoraSyncWord = 0x2B;
constexpr std::uint16_t kMeshtasticLoraPreambleLen = 16;
constexpr std::uint8_t kMeshtasticLoraCrcLen = 2;

struct RadioConfig
{
    meshtastic_Config_LoRaConfig_RegionCode region_code =
        meshtastic_Config_LoRaConfig_RegionCode_CN;
    meshtastic_Config_LoRaConfig_ModemPreset modem_preset =
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    bool using_preset = true;
    const char* channel_name = "LongFast";
    std::uint32_t channel_slot = 0;
    float freq_mhz = 0.0f;
    float bw_khz = 250.0f;
    std::uint8_t sf = 11;
    std::uint8_t cr_denom = 5;
    std::int8_t tx_power_dbm = 17;
    std::uint16_t preamble_len = kMeshtasticLoraPreambleLen;
    std::uint8_t sync_word = kMeshtasticLoraSyncWord;
    std::uint8_t crc_len = kMeshtasticLoraCrcLen;
};

const char* primaryChannelName(const MeshConfig& config);
const char* secondaryChannelName(const MeshConfig& config);
const char* channelName(const MeshConfig& config, ChannelId channel);
RadioConfig deriveRadioConfig(const MeshConfig& config);

} // namespace meshtastic
} // namespace chat
