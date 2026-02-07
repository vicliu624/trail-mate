/**
 * @file node_info_page_components.cpp
 * @brief Node info page UI components
 */

#include "node_info_page_components.h"
#include "node_info_page_layout.h"
#include "../../ui_common.h"
#include "../../../app/app_context.h"
#include "../../../chat/infra/meshtastic/mt_region.h"
#include "../../widgets/top_bar.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace node_info
{
namespace ui
{

namespace
{
NodeInfoWidgets s_widgets;
::ui::widgets::TopBar s_top_bar;

static const lv_color_t kColorAccent = lv_color_hex(0xEBA341);
static const lv_color_t kColorText = lv_color_hex(0x3A2A1A);
static const lv_color_t kColorTextMuted = lv_color_hex(0x6A5646);
static const lv_color_t kColorPageBg = lv_color_hex(0xFFF3DF);
static const lv_color_t kColorCardBg = lv_color_hex(0xFFF7E9);
static const lv_color_t kColorCardBorder = lv_color_hex(0xD9B06A);
static const lv_color_t kColorSeparator = lv_color_hex(0xE8D2AB);
static const lv_color_t kColorInfoBar = lv_color_hex(0xFFF0D3);
static const lv_color_t kColorAvatarBg = lv_color_hex(0x5BAF4A);
static const lv_color_t kColorMapBg = lv_color_hex(0xF6E7C8);
static const lv_color_t kColorMapAccent = lv_color_hex(0x2F6FD6);

void apply_root_style(lv_obj_t* obj)
{
    lv_obj_set_style_bg_color(obj, kColorPageBg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

void apply_card_style(lv_obj_t* obj)
{
    lv_obj_set_style_bg_color(obj, kColorCardBg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, kColorCardBorder, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void apply_title_bar_style(lv_obj_t* obj)
{
    lv_obj_set_style_bg_color(obj, kColorAccent, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void apply_info_bar_style(lv_obj_t* obj, int radius)
{
    lv_obj_set_style_bg_color(obj, kColorInfoBar, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, kColorSeparator, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void apply_map_style(lv_obj_t* obj)
{
    lv_obj_set_style_bg_color(obj, kColorMapBg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, kColorSeparator, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void apply_row_style(lv_obj_t* obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t* create_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (label && text)
    {
        lv_label_set_text(label, text);
    }
}

void format_node_id(uint32_t node_id, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (node_id <= 0xFFFFFF)
    {
        snprintf(out, out_len, "ID: !%06lx", static_cast<unsigned long>(node_id));
    }
    else
    {
        snprintf(out, out_len, "ID: !%08lx", static_cast<unsigned long>(node_id));
    }
}

void format_age(const char* prefix, uint32_t ts, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (ts == 0)
    {
        snprintf(out, out_len, "%s -", prefix);
        return;
    }
    uint32_t now = time(nullptr);
    if (now < ts)
    {
        snprintf(out, out_len, "%s 0s", prefix);
        return;
    }
    uint32_t age = now - ts;
    if (age < 60)
    {
        snprintf(out, out_len, "%s %us", prefix, static_cast<unsigned>(age));
        return;
    }
    if (age < 3600)
    {
        snprintf(out, out_len, "%s %um", prefix, static_cast<unsigned>(age / 60));
        return;
    }
    if (age < 86400)
    {
        snprintf(out, out_len, "%s %uh", prefix, static_cast<unsigned>(age / 3600));
        return;
    }
    snprintf(out, out_len, "%s %ud", prefix, static_cast<unsigned>(age / 86400));
}

void format_radio_params(char* ch_out, size_t ch_len, char* sf_out, size_t sf_len,
                         char* bw_out, size_t bw_len)
{
    if (!ch_out || ch_len == 0 || !sf_out || sf_len == 0 || !bw_out || bw_len == 0)
    {
        return;
    }

    ch_out[0] = '\0';
    sf_out[0] = '\0';
    bw_out[0] = '\0';

    const auto& cfg = app::AppContext::getInstance().getConfig();
    auto region_code = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.mesh_config.region);
    if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
    {
        region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
    }
    const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);
    if (!region)
    {
        snprintf(ch_out, ch_len, "Ch: -");
        snprintf(sf_out, sf_len, "SF: -");
        snprintf(bw_out, bw_len, "BW: -");
        return;
    }

    float bw_khz = 250.0f;
    uint8_t sf = 11;
    auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.mesh_config.modem_preset);
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bw_khz = region->wide_lora ? 1625.0f : 500.0f;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bw_khz = region->wide_lora ? 406.25f : 125.0f;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bw_khz = region->wide_lora ? 406.25f : 125.0f;
        sf = 12;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        bw_khz = region->wide_lora ? 812.5f : 250.0f;
        sf = 11;
        break;
    }

    const char* channel_name = chat::meshtastic::presetDisplayName(preset);
    float freq_mhz = chat::meshtastic::computeFrequencyMhz(region, bw_khz, channel_name);
    if (freq_mhz <= 0.0f)
    {
        freq_mhz = region->freq_start_mhz + (bw_khz / 2000.0f);
    }

    snprintf(ch_out, ch_len, "Ch: %.3f", static_cast<double>(freq_mhz));
    snprintf(sf_out, sf_len, "SF: %u", static_cast<unsigned>(sf));
    float bw_round = std::round(bw_khz);
    if (std::fabs(bw_khz - bw_round) < 0.05f)
    {
        snprintf(bw_out, bw_len, "BW: %.0fk", static_cast<double>(bw_khz));
    }
    else
    {
        snprintf(bw_out, bw_len, "BW: %.1fk", static_cast<double>(bw_khz));
    }
}

const char* role_to_text(chat::contacts::NodeRoleType role)
{
    using chat::contacts::NodeRoleType;
    switch (role)
    {
    case NodeRoleType::Client:
        return "Client";
    case NodeRoleType::ClientMute:
        return "ClientMute";
    case NodeRoleType::Router:
        return "Router";
    case NodeRoleType::RouterClient:
        return "RouterClient";
    case NodeRoleType::Repeater:
        return "Repeater";
    case NodeRoleType::Tracker:
        return "Tracker";
    case NodeRoleType::Sensor:
        return "Sensor";
    case NodeRoleType::Tak:
        return "TAK";
    case NodeRoleType::ClientHidden:
        return "ClientHidden";
    case NodeRoleType::LostAndFound:
        return "Lost&Found";
    case NodeRoleType::TakTracker:
        return "TAKTracker";
    case NodeRoleType::RouterLate:
        return "RouterLate";
    case NodeRoleType::ClientBase:
        return "ClientBase";
    case NodeRoleType::Unknown:
    default:
        return "-";
    }
}

double compute_accuracy_m(const chat::contacts::NodePosition& pos)
{
    if (pos.gps_accuracy_mm == 0)
    {
        return -1.0;
    }
    double acc = static_cast<double>(pos.gps_accuracy_mm) / 1000.0;
    uint32_t dop = pos.pdop ? pos.pdop : (pos.hdop ? pos.hdop : pos.vdop);
    if (dop > 0)
    {
        acc *= static_cast<double>(dop) / 100.0;
    }
    return acc;
}
}

NodeInfoWidgets create(lv_obj_t* parent)
{
    static const lv_point_precise_t kArrowPoints[] = {
        {10, 0},
        {20, 20},
        {0, 20},
        {10, 0},
    };

    s_widgets = NodeInfoWidgets{};
    s_widgets.root = layout::create_root(parent);
    s_widgets.header = layout::create_header(s_widgets.root);
    s_widgets.content = layout::create_content(s_widgets.root);
    s_widgets.top_row = layout::create_top_row(s_widgets.content);
    s_widgets.info_card = layout::create_info_card(s_widgets.top_row);
    s_widgets.info_header = layout::create_info_header(s_widgets.info_card);
    s_widgets.info_footer = layout::create_info_footer(s_widgets.info_card);
    s_widgets.location_card = layout::create_location_card(s_widgets.top_row);
    s_widgets.location_header = layout::create_location_header(s_widgets.location_card);
    s_widgets.location_map = layout::create_location_map(s_widgets.location_card);
    s_widgets.location_coords = layout::create_location_coords(s_widgets.location_card);
    s_widgets.location_updated = layout::create_location_updated(s_widgets.location_card);
    s_widgets.link_panel = layout::create_link_panel(s_widgets.content);
    s_widgets.link_header = layout::create_link_header(s_widgets.link_panel);
    s_widgets.link_row_1 = layout::create_link_row(s_widgets.link_panel);
    s_widgets.link_row_2 = layout::create_link_row(s_widgets.link_panel);

    apply_root_style(s_widgets.root);
    apply_row_style(s_widgets.content);
    apply_row_style(s_widgets.top_row);

    // Header (TopBar) - align with shared widget
    lv_obj_set_style_bg_opa(s_widgets.header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_widgets.header, 0, 0);
    lv_obj_set_style_pad_all(s_widgets.header, 0, 0);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(s_top_bar, s_widgets.header, cfg);
    ::ui::widgets::top_bar_set_title(s_top_bar, "NODE INFO");
    ui_update_top_bar_battery(s_top_bar);

    s_widgets.back_btn = s_top_bar.back_btn;
    s_widgets.title_label = s_top_bar.title_label;
    s_widgets.battery_label = s_top_bar.right_label;

    if (s_top_bar.container)
    {
        lv_obj_set_style_border_width(s_top_bar.container, 1, 0);
        lv_obj_set_style_border_color(s_top_bar.container, kColorCardBorder, 0);
        lv_obj_set_style_border_side(s_top_bar.container, LV_BORDER_SIDE_BOTTOM, 0);
    }
    if (s_widgets.title_label)
    {
        lv_obj_set_style_text_color(s_widgets.title_label, kColorText, 0);
    }
    if (s_widgets.battery_label)
    {
        lv_obj_set_style_text_color(s_widgets.battery_label, kColorText, 0);
    }
    if (s_widgets.back_btn)
    {
        lv_obj_t* back_label = lv_obj_get_child(s_widgets.back_btn, 0);
        if (back_label)
        {
            lv_obj_set_style_text_color(back_label, kColorText, 0);
            s_widgets.back_label = back_label;
        }
    }

    // Info card
    apply_card_style(s_widgets.info_card);
    apply_row_style(s_widgets.info_header);
    apply_info_bar_style(s_widgets.info_footer, 8);

    s_widgets.avatar_bg = lv_obj_create(s_widgets.info_header);
    lv_obj_set_pos(s_widgets.avatar_bg, 10, 10);
    lv_obj_set_size(s_widgets.avatar_bg, 46, 46);
    lv_obj_set_style_radius(s_widgets.avatar_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_widgets.avatar_bg, kColorAvatarBg, 0);
    lv_obj_set_style_bg_opa(s_widgets.avatar_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_widgets.avatar_bg, 2, 0);
    lv_obj_set_style_border_color(s_widgets.avatar_bg, kColorCardBorder, 0);
    s_widgets.avatar_label = create_label(s_widgets.avatar_bg, "S", &lv_font_montserrat_24, lv_color_hex(0xFFFFFF));
    lv_obj_center(s_widgets.avatar_label);

    s_widgets.name_label = create_label(s_widgets.info_header, "ALFA-3", &lv_font_montserrat_22, kColorText);
    lv_obj_set_pos(s_widgets.name_label, 62, 12);
    lv_obj_set_size(s_widgets.name_label, 118, 24);
    lv_label_set_long_mode(s_widgets.name_label, LV_LABEL_LONG_DOT);

    s_widgets.desc_label =
        create_label(s_widgets.info_header, "Alpha Team Relay Node", &lv_font_montserrat_14, kColorTextMuted);
    lv_obj_set_pos(s_widgets.desc_label, 62, 40);
    lv_obj_set_size(s_widgets.desc_label, 118, 18);
    lv_label_set_long_mode(s_widgets.desc_label, LV_LABEL_LONG_DOT);

    s_widgets.id_label = create_label(s_widgets.info_footer, "ID: !a1b2c3", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.id_label, 6, 4);
    s_widgets.role_label = create_label(s_widgets.info_footer, "Role: -", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.role_label, 6, 18);

    // Location card
    apply_card_style(s_widgets.location_card);
    apply_title_bar_style(s_widgets.location_header);
    s_widgets.location_title_label =
        create_label(s_widgets.location_header, "Location", &lv_font_montserrat_16, kColorText);
    lv_obj_set_pos(s_widgets.location_title_label, 10, 1);

    apply_map_style(s_widgets.location_map);
    s_widgets.map_label =
        create_label(s_widgets.location_map, "", &lv_font_montserrat_12, kColorTextMuted);
    lv_obj_center(s_widgets.map_label);

    lv_obj_t* cross_h = lv_obj_create(s_widgets.location_map);
    lv_obj_set_pos(cross_h, 103, 25);
    lv_obj_set_size(cross_h, 40, 2);
    lv_obj_set_style_bg_color(cross_h, kColorTextMuted, 0);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cross_h, 0, 0);

    lv_obj_t* cross_v = lv_obj_create(s_widgets.location_map);
    lv_obj_set_pos(cross_v, 122, 6);
    lv_obj_set_size(cross_v, 2, 40);
    lv_obj_set_style_bg_color(cross_v, kColorTextMuted, 0);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cross_v, 0, 0);

    lv_obj_t* acc_circle = lv_obj_create(s_widgets.location_map);
    lv_obj_set_pos(acc_circle, 103, 6);
    lv_obj_set_size(acc_circle, 40, 40);
    lv_obj_set_style_bg_opa(acc_circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(acc_circle, 1, 0);
    lv_obj_set_style_border_color(acc_circle, kColorMapAccent, 0);
    lv_obj_set_style_radius(acc_circle, LV_RADIUS_CIRCLE, 0);

    lv_obj_t* arrow = lv_line_create(s_widgets.location_map);
    lv_line_set_points(arrow, kArrowPoints, 4);
    lv_obj_set_pos(arrow, 113, 16);
    lv_obj_set_style_line_width(arrow, 2, 0);
    lv_obj_set_style_line_color(arrow, kColorMapAccent, 0);
    lv_obj_set_style_line_rounded(arrow, true, 0);

    apply_info_bar_style(s_widgets.location_coords, 6);
    s_widgets.coords_latlon_label =
        create_label(s_widgets.location_coords, "35.65858, 139.74543", &lv_font_montserrat_14, kColorText);
    lv_obj_set_pos(s_widgets.coords_latlon_label, 6, -1);
    lv_obj_set_size(s_widgets.coords_latlon_label, 140, 14);
    lv_label_set_long_mode(s_widgets.coords_latlon_label, LV_LABEL_LONG_DOT);

    s_widgets.coords_acc_label =
        create_label(s_widgets.location_coords, "+/- 12 m", &lv_font_montserrat_14, kColorText);
    lv_obj_set_pos(s_widgets.coords_acc_label, 150, -1);
    lv_obj_set_size(s_widgets.coords_acc_label, 45, 14);
    lv_obj_set_style_text_align(s_widgets.coords_acc_label, LV_TEXT_ALIGN_RIGHT, 0);

    s_widgets.coords_alt_label =
        create_label(s_widgets.location_coords, "Alt: 43 m", &lv_font_montserrat_14, kColorText);
    lv_obj_set_pos(s_widgets.coords_alt_label, 195, -1);
    lv_obj_set_size(s_widgets.coords_alt_label, 46, 14);
    lv_obj_set_style_text_align(s_widgets.coords_alt_label, LV_TEXT_ALIGN_RIGHT, 0);

    apply_row_style(s_widgets.location_updated);
    s_widgets.updated_label =
        create_label(s_widgets.location_updated, "Updated: 2m ago", &lv_font_montserrat_14, kColorTextMuted);
    lv_obj_set_size(s_widgets.updated_label, 246, 18);
    lv_obj_set_style_text_align(s_widgets.updated_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_widgets.updated_label, LV_ALIGN_CENTER, 0, -3);
    lv_obj_add_flag(s_widgets.location_updated, LV_OBJ_FLAG_HIDDEN);

    // Link panel
    apply_card_style(s_widgets.link_panel);
    apply_title_bar_style(s_widgets.link_header);
    s_widgets.link_title_label = create_label(s_widgets.link_header, "Link", &lv_font_montserrat_16, kColorText);
    lv_obj_set_pos(s_widgets.link_title_label, 10, 1);
    lv_obj_set_size(s_widgets.link_title_label, 440, 18);
    lv_label_set_long_mode(s_widgets.link_title_label, LV_LABEL_LONG_DOT);

    apply_row_style(s_widgets.link_row_1);
    lv_obj_set_pos(s_widgets.link_row_1, 0, 22);
    s_widgets.link_rssi_label =
        create_label(s_widgets.link_row_1, "RSSI: -112 dBm", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.link_rssi_label, 10, -3);
    s_widgets.link_snr_label =
        create_label(s_widgets.link_row_1, "SNR: 7.5 dB", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.link_snr_label, 140, -3);
    s_widgets.link_ch_label =
        create_label(s_widgets.link_row_1, "Ch: 478.875", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.link_ch_label, 250, -3);
    s_widgets.link_sf_label =
        create_label(s_widgets.link_row_1, "SF: 7", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.link_sf_label, 330, -3);
    s_widgets.link_bw_label =
        create_label(s_widgets.link_row_1, "BW: 125k", &lv_font_montserrat_12, kColorText);
    lv_obj_set_pos(s_widgets.link_bw_label, 390, -3);

    apply_row_style(s_widgets.link_row_2);
    lv_obj_set_pos(s_widgets.link_row_2, 0, 38);
    s_widgets.link_hop_label =
        create_label(s_widgets.link_row_2, "Hop: 2", &lv_font_montserrat_12, kColorTextMuted);
    lv_obj_set_pos(s_widgets.link_hop_label, 10, -3);
    s_widgets.link_last_heard_label =
        create_label(s_widgets.link_row_2, "Last heard: 18s", &lv_font_montserrat_12, kColorTextMuted);
    lv_obj_set_pos(s_widgets.link_last_heard_label, 140, -3);

    return s_widgets;
}

void destroy()
{
    if (s_widgets.root && lv_obj_is_valid(s_widgets.root))
    {
        lv_obj_del(s_widgets.root);
    }
    s_widgets = NodeInfoWidgets{};
    s_top_bar = ::ui::widgets::TopBar{};
}

const NodeInfoWidgets& widgets()
{
    return s_widgets;
}

void set_node_info(const chat::contacts::NodeInfo& node)
{
    std::string name = node.display_name;
    if (name.empty())
    {
        name = node.short_name;
    }
    if (name.empty())
    {
        name = "Unknown";
    }

    const char* long_name = node.long_name;
    if (!long_name || long_name[0] == '\0' || name == long_name)
    {
        long_name = "";
    }

    char avatar_text[2] = "?";
    if (!name.empty() && std::isalnum(static_cast<unsigned char>(name[0])))
    {
        avatar_text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        avatar_text[1] = '\0';
    }

    char id_buf[24];
    format_node_id(node.node_id, id_buf, sizeof(id_buf));

    char role_buf[32];
    snprintf(role_buf, sizeof(role_buf), "Role: %s", role_to_text(node.role));

    set_label_text(s_widgets.avatar_label, avatar_text);
    set_label_text(s_widgets.name_label, name.c_str());
    set_label_text(s_widgets.desc_label, long_name);
    set_label_text(s_widgets.id_label, id_buf);
    set_label_text(s_widgets.role_label, role_buf);

    // Position / location
    if (node.position.valid)
    {
        double lat = static_cast<double>(node.position.latitude_i) * 1e-7;
        double lon = static_cast<double>(node.position.longitude_i) * 1e-7;

        char latlon_buf[48];
        snprintf(latlon_buf, sizeof(latlon_buf), "%.5f, %.5f", lat, lon);

        double acc_m = compute_accuracy_m(node.position);
        char acc_buf[24];
        if (acc_m >= 0.0)
        {
            snprintf(acc_buf, sizeof(acc_buf), "+/- %.0f m", acc_m);
        }
        else
        {
            snprintf(acc_buf, sizeof(acc_buf), "+/- -");
        }

        char alt_buf[24];
        if (node.position.has_altitude)
        {
            snprintf(alt_buf, sizeof(alt_buf), "Alt: %ld m", static_cast<long>(node.position.altitude));
        }
        else
        {
            snprintf(alt_buf, sizeof(alt_buf), "Alt: -");
        }

        set_label_text(s_widgets.coords_latlon_label, latlon_buf);
        set_label_text(s_widgets.coords_acc_label, acc_buf);
        set_label_text(s_widgets.coords_alt_label, alt_buf);
        set_label_text(s_widgets.map_label, "");
    }
    else
    {
        set_label_text(s_widgets.coords_latlon_label, "No position");
        set_label_text(s_widgets.coords_acc_label, "+/- -");
        set_label_text(s_widgets.coords_alt_label, "Alt: -");
        set_label_text(s_widgets.map_label, "No map");
    }

    uint32_t update_ts = node.position.timestamp ? node.position.timestamp : node.last_seen;
    set_label_text(s_widgets.link_title_label, "Link");

    // Link info (best-effort)
    char rssi_buf[24];
    if (std::isnan(node.rssi))
    {
        snprintf(rssi_buf, sizeof(rssi_buf), "RSSI: -");
    }
    else
    {
        snprintf(rssi_buf, sizeof(rssi_buf), "RSSI: %.0f dBm", node.rssi);
    }
    set_label_text(s_widgets.link_rssi_label, rssi_buf);
    char snr_buf[24];
    if (std::isnan(node.snr))
    {
        snprintf(snr_buf, sizeof(snr_buf), "SNR: -");
    }
    else
    {
        snprintf(snr_buf, sizeof(snr_buf), "SNR: %.1f dB", node.snr);
    }
    set_label_text(s_widgets.link_snr_label, snr_buf);
    char ch_buf[32];
    char sf_buf[16];
    char bw_buf[16];
    format_radio_params(ch_buf, sizeof(ch_buf), sf_buf, sizeof(sf_buf), bw_buf, sizeof(bw_buf));
    set_label_text(s_widgets.link_ch_label, ch_buf);
    set_label_text(s_widgets.link_sf_label, sf_buf);
    set_label_text(s_widgets.link_bw_label, bw_buf);

    char last_heard_buf[32];
    format_age("Last heard:", node.last_seen, last_heard_buf, sizeof(last_heard_buf));
    char hop_buf[16];
    if (node.hops_away != 0xFF)
    {
        snprintf(hop_buf, sizeof(hop_buf), "Hop: %u", static_cast<unsigned>(node.hops_away));
        set_label_text(s_widgets.link_hop_label, hop_buf);
    }
    else
    {
        set_label_text(s_widgets.link_hop_label, "Hop: -");
    }
    set_label_text(s_widgets.link_last_heard_label, last_heard_buf);
}

} // namespace ui
} // namespace node_info
