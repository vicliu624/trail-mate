#include "boards/gat562_mesh_evb_pro/settings_store.h"

#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"

namespace boards::gat562_mesh_evb_pro::settings_store
{
namespace
{

bool s_has_config = false;
app::AppConfig s_config{};
uint8_t s_tone_volume = 45;

int8_t clampTxPower(int8_t value)
{
    if (value < app::AppConfig::kTxPowerMinDbm)
    {
        return app::AppConfig::kTxPowerMinDbm;
    }
    if (value > app::AppConfig::kTxPowerMaxDbm)
    {
        return app::AppConfig::kTxPowerMaxDbm;
    }
    return value;
}

} // namespace

void normalizeConfig(app::AppConfig& config)
{
    if (!chat::infra::isValidMeshProtocol(config.mesh_protocol))
    {
        config.mesh_protocol = chat::MeshProtocol::Meshtastic;
    }

    if (chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.meshtastic_config.region)) == nullptr)
    {
        config.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }

    config.meshtastic_config.tx_power = clampTxPower(config.meshtastic_config.tx_power);
    config.meshcore_config.tx_power = clampTxPower(config.meshcore_config.tx_power);

    if (chat::meshcore::isValidRegionPresetId(config.meshcore_config.meshcore_region_preset) &&
        config.meshcore_config.meshcore_region_preset > 0)
    {
        if (const chat::meshcore::RegionPreset* preset =
                chat::meshcore::findRegionPresetById(config.meshcore_config.meshcore_region_preset))
        {
            config.meshcore_config.meshcore_freq_mhz = preset->freq_mhz;
            config.meshcore_config.meshcore_bw_khz = preset->bw_khz;
            config.meshcore_config.meshcore_sf = preset->sf;
            config.meshcore_config.meshcore_cr = preset->cr;
        }
    }
    else
    {
        config.meshcore_config.meshcore_region_preset = 0;
    }
}

bool loadAppConfig(app::AppConfig& config)
{
    if (s_has_config)
    {
        config = s_config;
        normalizeConfig(config);
        return true;
    }

    normalizeConfig(config);
    return false;
}

bool saveAppConfig(const app::AppConfig& config)
{
    s_config = config;
    normalizeConfig(s_config);
    s_has_config = true;
    return true;
}

uint8_t loadMessageToneVolume()
{
    return s_tone_volume;
}

bool saveMessageToneVolume(uint8_t volume)
{
    s_tone_volume = volume;
    return true;
}

} // namespace boards::gat562_mesh_evb_pro::settings_store
