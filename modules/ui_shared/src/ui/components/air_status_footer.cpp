#include "ui/components/air_status_footer.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/two_pane_layout.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace ui::components::air_status_footer
{
namespace
{

constexpr lv_coord_t kFooterHeightCompact = 36;
constexpr lv_coord_t kFooterHeightLarge = 44;

void format_bw_text(float bw_khz, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (!std::isfinite(bw_khz) || bw_khz <= 0.0f)
    {
        std::snprintf(out, out_len, "BW-");
        return;
    }

    const float rounded = std::round(bw_khz);
    if (std::fabs(bw_khz - rounded) < 0.05f)
    {
        std::snprintf(out, out_len, "BW%.0fk", static_cast<double>(bw_khz));
    }
    else
    {
        std::snprintf(out, out_len, "BW%.1fk", static_cast<double>(bw_khz));
    }
}

void format_summary_and_detail(char* summary,
                               size_t summary_len,
                               char* detail,
                               size_t detail_len)
{
    if (!summary || summary_len == 0 || !detail || detail_len == 0)
    {
        return;
    }

    summary[0] = '\0';
    detail[0] = '\0';

    const app::AppConfig& cfg = app::configFacade().getConfig();
    const chat::MeshProtocol protocol = cfg.mesh_protocol;
    const char* protocol_name = chat::infra::meshProtocolName(protocol);

    float freq_mhz = 0.0f;
    float bw_khz = 0.0f;
    uint8_t sf = 0;
    uint8_t cr = 0;

    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        const auto region_code =
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region);
        const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);
        if (cfg.meshtastic_config.use_preset && region)
        {
            const auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(
                cfg.meshtastic_config.modem_preset);
            chat::meshtastic::modemPresetToParams(
                preset, region->wide_lora, bw_khz, sf, cr);
            const char* channel_name = chat::meshtastic::presetDisplayName(preset);
            freq_mhz = chat::meshtastic::computeFrequencyMhz(region, bw_khz, channel_name);
        }
        else
        {
            bw_khz = cfg.meshtastic_config.bandwidth_khz;
            sf = cfg.meshtastic_config.spread_factor;
            cr = cfg.meshtastic_config.coding_rate;
            freq_mhz = cfg.meshtastic_config.override_frequency_mhz;
        }
        freq_mhz += cfg.meshtastic_config.frequency_offset_mhz;
        if (freq_mhz <= 0.0f)
        {
            freq_mhz = chat::meshtastic::estimateFrequencyMhz(
                cfg.meshtastic_config.region, cfg.meshtastic_config.modem_preset);
        }
    }
    else if (protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshConfig& mesh = cfg.meshcore_config;
        freq_mhz = mesh.meshcore_freq_mhz;
        bw_khz = mesh.meshcore_bw_khz;
        sf = mesh.meshcore_sf;
        cr = mesh.meshcore_cr;

        if (mesh.meshcore_region_preset > 0)
        {
            if (const chat::meshcore::RegionPreset* preset =
                    chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset))
            {
                freq_mhz = preset->freq_mhz;
                bw_khz = preset->bw_khz;
                sf = preset->sf;
                cr = preset->cr;
            }
        }
    }
    else
    {
        const chat::MeshConfig& mesh = cfg.rnode_config;
        freq_mhz = mesh.override_frequency_mhz;
        bw_khz = mesh.bandwidth_khz;
        sf = mesh.spread_factor;
        cr = mesh.coding_rate;
    }

    if (freq_mhz > 0.0f)
    {
        std::snprintf(summary,
                      summary_len,
                      "%s  %.3fMHz",
                      protocol_name ? protocol_name : "-",
                      static_cast<double>(freq_mhz));
    }
    else
    {
        std::snprintf(summary, summary_len, "%s", protocol_name ? protocol_name : "-");
    }

    char bw_text[24];
    format_bw_text(bw_khz, bw_text, sizeof(bw_text));
    std::snprintf(detail,
                  detail_len,
                  "%s  SF%u  CR%u",
                  bw_text,
                  static_cast<unsigned>(sf),
                  static_cast<unsigned>(cr));
}

} // namespace

Footer create(lv_obj_t* parent)
{
    Footer footer{};
    if (!parent)
    {
        return footer;
    }

    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t footer_height =
        profile.large_touch_hitbox ? kFooterHeightLarge : kFooterHeightCompact;

    footer.container = lv_obj_create(parent);
    lv_obj_set_size(footer.container, LV_PCT(100), footer_height);
    ::ui::components::two_pane_layout::make_non_scrollable(footer.container);
    ::ui::components::two_pane_styles::apply_container_main(footer.container);
    lv_obj_set_style_border_width(footer.container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        footer.container,
        lv_color_hex(::ui::components::two_pane_styles::kBorder),
        LV_PART_MAIN);
    lv_obj_set_style_radius(footer.container, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(footer.container, profile.large_touch_hitbox ? 12 : 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(footer.container, profile.large_touch_hitbox ? 12 : 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(footer.container, profile.large_touch_hitbox ? 6 : 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(footer.container, profile.large_touch_hitbox ? 6 : 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(footer.container, 1, LV_PART_MAIN);
    lv_obj_set_flex_flow(footer.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        footer.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    footer.summary_label = lv_label_create(footer.container);
    lv_obj_set_width(footer.summary_label, LV_PCT(100));
    lv_label_set_long_mode(footer.summary_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(footer.summary_label, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::components::two_pane_styles::apply_label_primary(footer.summary_label);
    ::ui::fonts::apply_font(footer.summary_label, &lv_font_montserrat_14);

    footer.detail_label = lv_label_create(footer.container);
    lv_obj_set_width(footer.detail_label, LV_PCT(100));
    lv_label_set_long_mode(footer.detail_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(footer.detail_label, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::components::two_pane_styles::apply_label_muted(footer.detail_label);
    ::ui::fonts::apply_font(footer.detail_label, &lv_font_montserrat_14);

    refresh(footer);
    return footer;
}

void refresh(Footer& footer)
{
    if (!footer.summary_label || !footer.detail_label)
    {
        return;
    }

    char summary[64];
    char detail[48];
    format_summary_and_detail(summary, sizeof(summary), detail, sizeof(detail));

    ::ui::i18n::set_label_text_raw(footer.summary_label, summary);
    ::ui::i18n::set_label_text_raw(footer.detail_label, detail);
}

} // namespace ui::components::air_status_footer
