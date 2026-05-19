#include "chat/infra/meshtastic/mt_radio_config.h"

#include <cmath>

namespace chat
{
namespace meshtastic
{
namespace
{

std::uint32_t djb2Hash(const char* text)
{
    std::uint32_t hash = 5381U;
    if (text == nullptr)
    {
        return hash;
    }
    while (*text != '\0')
    {
        hash = ((hash << 5U) + hash) + static_cast<std::uint8_t>(*text);
        ++text;
    }
    return hash;
}

template <typename T>
T clampValue(T value, T min_value, T max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

float normalizeBandwidthKhz(float bw_khz)
{
    if (bw_khz == 31.0f) return 31.25f;
    if (bw_khz == 62.0f) return 62.5f;
    if (bw_khz == 200.0f) return 203.125f;
    if (bw_khz == 400.0f) return 406.25f;
    if (bw_khz == 800.0f) return 812.5f;
    if (bw_khz == 1600.0f) return 1625.0f;
    return bw_khz;
}

std::uint32_t channelCount(const RegionInfo& region, float bw_khz)
{
    const float span_mhz = region.freq_end_mhz - region.freq_start_mhz;
    const float spacing_mhz = region.spacing_khz / 1000.0f;
    const float bw_mhz = bw_khz / 1000.0f;
    std::uint32_t count =
        static_cast<std::uint32_t>(std::floor(span_mhz / (spacing_mhz + bw_mhz)));
    return count < 1U ? 1U : count;
}

} // namespace

const char* primaryChannelName(const MeshConfig& config)
{
    if (config.primary_channel_name[0] != '\0')
    {
        return config.primary_channel_name;
    }

    if (!config.use_preset)
    {
        return "Custom";
    }

    const auto preset =
        static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config.modem_preset);
    const char* name = presetDisplayName(preset);
    return (name != nullptr && name[0] != '\0') ? name : "Custom";
}

const char* secondaryChannelName(const MeshConfig& config)
{
    return (config.secondary_channel_name[0] != '\0') ? config.secondary_channel_name : "Secondary";
}

const char* channelName(const MeshConfig& config, ChannelId channel)
{
    return (channel == ChannelId::SECONDARY) ? secondaryChannelName(config) : primaryChannelName(config);
}

RadioConfig deriveRadioConfig(const MeshConfig& config)
{
    RadioConfig out{};

    out.region_code =
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.region);
    if (out.region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        out.region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }
    const RegionInfo* region = findRegion(out.region_code);
    if (region == nullptr)
    {
        region = findRegion(meshtastic_Config_LoRaConfig_RegionCode_CN);
        out.region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }

    out.modem_preset =
        static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(config.modem_preset);
    out.using_preset = config.use_preset;
    if (out.using_preset)
    {
        modemPresetToParams(out.modem_preset,
                            region != nullptr && region->wide_lora,
                            out.bw_khz,
                            out.sf,
                            out.cr_denom);
    }
    else
    {
        out.bw_khz = normalizeBandwidthKhz(config.bandwidth_khz);
        out.sf = clampValue<std::uint8_t>(config.spread_factor, 5, 12);
        out.cr_denom = clampValue<std::uint8_t>(config.coding_rate, 5, 8);
        if (region != nullptr)
        {
            if (out.bw_khz < 7.0f)
            {
                out.bw_khz = 7.8f;
            }
            if (!region->wide_lora && out.bw_khz > 500.0f)
            {
                out.bw_khz = 500.0f;
            }
            if (region->wide_lora && out.bw_khz > 1625.0f)
            {
                out.bw_khz = 1625.0f;
            }
        }
    }

    if (region != nullptr)
    {
        const float region_span_khz =
            (region->freq_end_mhz - region->freq_start_mhz) * 1000.0f;
        if (region_span_khz < out.bw_khz)
        {
            out.using_preset = true;
            out.modem_preset =
                meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
            modemPresetToParams(out.modem_preset,
                                region->wide_lora,
                                out.bw_khz,
                                out.sf,
                                out.cr_denom);
        }
    }

    out.channel_name = primaryChannelName(config);
    if (out.channel_name == nullptr || out.channel_name[0] == '\0')
    {
        out.channel_name = "Custom";
    }

    if (region != nullptr)
    {
        const std::uint32_t count = channelCount(*region, out.bw_khz);
        out.channel_slot =
            config.channel_num > 0
                ? static_cast<std::uint32_t>((config.channel_num - 1U) % count)
                : djb2Hash(out.channel_name) % count;

        out.freq_mhz = region->freq_start_mhz + (out.bw_khz / 2000.0f) +
                       (out.channel_slot * (out.bw_khz / 1000.0f));
    }

    if (config.override_frequency_mhz > 0.0f)
    {
        out.freq_mhz = config.override_frequency_mhz;
    }
    out.freq_mhz += config.frequency_offset_mhz;

    if (region != nullptr && config.override_frequency_mhz <= 0.0f)
    {
        float min_center = region->freq_start_mhz + (out.bw_khz / 2000.0f);
        float max_center = region->freq_end_mhz - (out.bw_khz / 2000.0f);
        if (min_center > max_center)
        {
            min_center = region->freq_start_mhz;
            max_center = region->freq_end_mhz;
        }
        out.freq_mhz = clampValue(out.freq_mhz, min_center, max_center);
    }

    out.tx_power_dbm = config.tx_power;
    if (region != nullptr && region->power_limit_dbm > 0U)
    {
        const auto limit = static_cast<std::int8_t>(region->power_limit_dbm);
        if (out.tx_power_dbm == 0)
        {
            out.tx_power_dbm = limit;
        }
        if (out.tx_power_dbm > limit)
        {
            out.tx_power_dbm = limit;
        }
    }
    if (out.tx_power_dbm == 0)
    {
        out.tx_power_dbm = 17;
    }
    if (out.tx_power_dbm < -9)
    {
        out.tx_power_dbm = -9;
    }

    return out;
}

} // namespace meshtastic
} // namespace chat
