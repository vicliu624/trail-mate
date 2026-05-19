#include "ui/presentation_sources/legacy_air_device_status_source.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_radio_config.h"

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace ui::presentation_sources
{
namespace
{

void formatBandwidth(float bandwidth_khz, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (!std::isfinite(bandwidth_khz) || bandwidth_khz <= 0.0f)
    {
        std::snprintf(out, out_len, "BW-");
        return;
    }

    const float rounded = std::round(bandwidth_khz);
    if (std::fabs(bandwidth_khz - rounded) < 0.05f)
    {
        std::snprintf(out, out_len, "BW%.0fk", static_cast<double>(bandwidth_khz));
    }
    else
    {
        std::snprintf(out, out_len, "BW%.1fk", static_cast<double>(bandwidth_khz));
    }
}

void copyRadioProjection(ui::device::DeviceStatusSnapshot& out,
                         const char* protocol_name,
                         float frequency_mhz,
                         float bandwidth_khz,
                         uint8_t spreading_factor,
                         uint8_t coding_rate)
{
    ui::copyText(out.active_protocol, protocol_name ? protocol_name : "-");

    char region[32];
    if (frequency_mhz > 0.0f)
    {
        std::snprintf(region, sizeof(region), "%.3fMHz", static_cast<double>(frequency_mhz));
    }
    else
    {
        std::snprintf(region, sizeof(region), "-");
    }
    ui::copyText(out.region, region);

    char bandwidth[24];
    formatBandwidth(bandwidth_khz, bandwidth, sizeof(bandwidth));

    char modem[32];
    std::snprintf(modem,
                  sizeof(modem),
                  "%s  SF%u  CR%u",
                  bandwidth,
                  static_cast<unsigned>(spreading_factor),
                  static_cast<unsigned>(coding_rate));
    ui::copyText(out.modem_preset, modem);

    char status[64];
    if (frequency_mhz > 0.0f)
    {
        std::snprintf(status,
                      sizeof(status),
                      "%s  %.3fMHz",
                      protocol_name ? protocol_name : "-",
                      static_cast<double>(frequency_mhz));
    }
    else
    {
        std::snprintf(status, sizeof(status), "%s", protocol_name ? protocol_name : "-");
    }
    ui::copyText(out.status_line, status);
}

} // namespace

bool LegacyAirDeviceStatusSource::buildDeviceStatusSnapshot(ui::device::DeviceStatusSnapshot& out) const
{
    out = ui::device::DeviceStatusSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.lora = ui::device::CapabilityDisplayState::Ready;
    out.storage = ui::device::CapabilityDisplayState::Ready;

    const app::AppConfig& config = app::configFacade().getConfig();
    const chat::MeshProtocol protocol = config.mesh_protocol;
    const char* protocol_name = chat::infra::meshProtocolName(protocol);

    float frequency_mhz = 0.0f;
    float bandwidth_khz = 0.0f;
    uint8_t spreading_factor = 0;
    uint8_t coding_rate = 0;

    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        const chat::meshtastic::RadioConfig radio =
            chat::meshtastic::deriveRadioConfig(config.meshtastic_config);
        frequency_mhz = radio.freq_mhz;
        bandwidth_khz = radio.bw_khz;
        spreading_factor = radio.sf;
        coding_rate = radio.cr_denom;
    }
    else if (protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshConfig& mesh = config.meshcore_config;
        frequency_mhz = mesh.meshcore_freq_mhz;
        bandwidth_khz = mesh.meshcore_bw_khz;
        spreading_factor = mesh.meshcore_sf;
        coding_rate = mesh.meshcore_cr;

        if (mesh.meshcore_region_preset > 0)
        {
            if (const chat::meshcore::RegionPreset* preset =
                    chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset))
            {
                frequency_mhz = preset->freq_mhz;
                bandwidth_khz = preset->bw_khz;
                spreading_factor = preset->sf;
                coding_rate = preset->cr;
            }
        }
    }
    else
    {
        const chat::MeshConfig& mesh = config.rnode_config;
        frequency_mhz = mesh.override_frequency_mhz;
        bandwidth_khz = mesh.bandwidth_khz;
        spreading_factor = mesh.spread_factor;
        coding_rate = mesh.coding_rate;
    }

    copyRadioProjection(out,
                        protocol_name,
                        frequency_mhz,
                        bandwidth_khz,
                        spreading_factor,
                        coding_rate);
    return true;
}

LegacyAirDeviceStatusSource& legacy_air_device_status_source()
{
    static LegacyAirDeviceStatusSource source;
    return source;
}

} // namespace ui::presentation_sources
