/**
 * @file settings_page_components.cpp
 * @brief Settings UI components implementation
 */

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../../app/app_context.h"
#include "../../../board/BoardBase.h"
#include "../../../chat/domain/chat_types.h"
#include "../../../chat/infra/meshcore/mc_region_presets.h"
#include "../../../chat/infra/meshtastic/generated/meshtastic/config.pb.h"
#include "../../../chat/infra/meshtastic/mt_region.h"
#include "../../../gps/gps_service_api.h"
#include "../../../gps/usecase/track_recorder.h"
#include "../../ui_common.h"
#include "../../widgets/system_notification.h"
#include "settings_page_components.h"
#include "settings_page_input.h"
#include "settings_page_layout.h"
#include "settings_page_styles.h"
#include "settings_state.h"

extern uint32_t getScreenSleepTimeout();
extern void setScreenSleepTimeout(uint32_t timeout_ms);

namespace settings::ui::components
{

namespace
{

constexpr size_t kMaxItems = 32;
constexpr size_t kMaxOptions = 40;
constexpr const char* kPrefsNs = "settings";
constexpr int kNetTxPowerMin = -9;
constexpr int kNetTxPowerMax = 30;

struct CategoryDef
{
    const char* label;
    const settings::ui::SettingItem* items;
    size_t item_count;
};

struct OptionClick
{
    const settings::ui::SettingItem* item;
    int value;
    settings::ui::ItemWidget* widget;
};

static OptionClick s_option_clicks[kMaxOptions]{};
static size_t s_option_click_count = 0;
static lv_group_t* s_modal_prev_group = nullptr;
static int s_pending_category = -1;
static bool s_category_update_scheduled = false;
static bool s_building_list = false;
static settings::ui::SettingOption kChatRegionOptions[32] = {};
static size_t kChatRegionOptionCount = 0;
static settings::ui::SettingOption kMeshCoreRegionPresetOptions[32] = {};
static size_t kMeshCoreRegionPresetOptionCount = 0;

static void build_item_list();

static void prefs_put_int(const char* key, int value)
{
    Preferences prefs;
    prefs.begin(kPrefsNs, false);
    prefs.putInt(key, value);
    prefs.end();
}

static void prefs_put_bool(const char* key, bool value)
{
    Preferences prefs;
    prefs.begin(kPrefsNs, false);
    prefs.putBool(key, value);
    prefs.end();
}

static int prefs_get_int(const char* key, int default_value)
{
    Preferences prefs;
    prefs.begin(kPrefsNs, true);
    int value = prefs.getInt(key, default_value);
    prefs.end();
    return value;
}

static bool prefs_get_bool(const char* key, bool default_value)
{
    Preferences prefs;
    prefs.begin(kPrefsNs, true);
    bool value = prefs.getBool(key, default_value);
    prefs.end();
    return value;
}

static void prefs_get_str(const char* key, char* out, size_t out_len, const char* default_value)
{
    if (!out || out_len == 0)
    {
        return;
    }
    Preferences prefs;
    prefs.begin(kPrefsNs, true);
    String value = prefs.getString(key, default_value ? default_value : "");
    prefs.end();
    strncpy(out, value.c_str(), out_len - 1);
    out[out_len - 1] = '\0';
}

static bool is_zero_key(const uint8_t* key, size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0)
        {
            return false;
        }
    }
    return true;
}

static void bytes_to_hex(const uint8_t* data, size_t len, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!data || len == 0)
    {
        return;
    }
    static const char* kHex = "0123456789ABCDEF";
    size_t required = len * 2 + 1;
    if (out_len < required)
    {
        return;
    }
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t b = data[i];
        out[i * 2] = kHex[b >> 4];
        out[i * 2 + 1] = kHex[b & 0x0F];
    }
    out[len * 2] = '\0';
}

static bool parse_hex_char(char c, uint8_t& out)
{
    if (c >= '0' && c <= '9')
    {
        out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        out = static_cast<uint8_t>(10 + (c - 'a'));
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        out = static_cast<uint8_t>(10 + (c - 'A'));
        return true;
    }
    return false;
}

static bool parse_psk(const char* text, uint8_t* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    if (!text || text[0] == '\0')
    {
        memset(out, 0, out_len);
        return true;
    }
    size_t len = strlen(text);
    if (len == 32)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            uint8_t hi = 0;
            uint8_t lo = 0;
            if (!parse_hex_char(text[i * 2], hi) || !parse_hex_char(text[i * 2 + 1], lo))
            {
                return false;
            }
            out[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }
    if (len == 16)
    {
        memcpy(out, text, 16);
        return true;
    }
    return false;
}

static void float_to_text(float value, char* out, size_t out_len, uint8_t decimals = 2)
{
    if (!out || out_len == 0)
    {
        return;
    }
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%uf", static_cast<unsigned>(decimals));
    snprintf(out, out_len, fmt, static_cast<double>(value));
}

static bool parse_float_text(const char* text, float* out_value)
{
    if (!text || !out_value)
    {
        return false;
    }
    char* end = nullptr;
    float value = strtof(text, &end);
    if (end == text || (end && *end != '\0') || !std::isfinite(value))
    {
        return false;
    }
    *out_value = value;
    return true;
}

static void mark_restart_required()
{
    g_settings.needs_restart = true;
    prefs_put_bool("needs_restart", true);
    ::ui::SystemNotification::show("Restart required", 4000);
}

static void reset_mesh_settings()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    app_ctx.getConfig().meshtastic_config = chat::MeshConfig();
    app_ctx.getConfig().meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    app_ctx.getConfig().meshcore_config = chat::MeshConfig();
    strncpy(app_ctx.getConfig().meshcore_config.meshcore_channel_name, "Public",
            sizeof(app_ctx.getConfig().meshcore_config.meshcore_channel_name) - 1);
    app_ctx.getConfig().meshcore_config.meshcore_channel_name[sizeof(app_ctx.getConfig().meshcore_config.meshcore_channel_name) - 1] = '\0';
    app_ctx.saveConfig();
    app_ctx.applyMeshConfig();

    g_settings.chat_protocol = static_cast<int>(app_ctx.getConfig().mesh_protocol);
    g_settings.chat_region = app_ctx.getConfig().meshtastic_config.region;
    g_settings.chat_channel = 0;
    g_settings.chat_psk[0] = '\0';
    g_settings.net_use_preset = app_ctx.getConfig().meshtastic_config.use_preset;
    g_settings.net_modem_preset = app_ctx.getConfig().meshtastic_config.modem_preset;
    g_settings.net_tx_power = app_ctx.getConfig().meshtastic_config.tx_power;
    g_settings.net_hop_limit = app_ctx.getConfig().meshtastic_config.hop_limit;
    const chat::MeshConfig& active_cfg =
        (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::MeshCore)
            ? app_ctx.getConfig().meshcore_config
            : app_ctx.getConfig().meshtastic_config;
    g_settings.net_tx_enabled = active_cfg.tx_enabled;
    g_settings.net_relay = app_ctx.getConfig().meshtastic_config.enable_relay;
    g_settings.net_duty_cycle = true;
    g_settings.net_channel_util = 0;
    g_settings.mc_region_preset = app_ctx.getConfig().meshcore_config.meshcore_region_preset;
    g_settings.needs_restart = false;

    Preferences prefs;
    prefs.begin(kPrefsNs, false);
    prefs.remove("mesh_protocol");
    prefs.remove("chat_region");
    prefs.remove("chat_channel");
    prefs.remove("chat_psk");
    prefs.remove("mc_channel_name");
    prefs.remove("mc_channel_key");
    prefs.remove("mc_ch_name");
    prefs.remove("mc_ch_key");
    prefs.remove("net_preset");
    prefs.remove("net_use_preset");
    prefs.remove("net_bw");
    prefs.remove("net_sf");
    prefs.remove("net_cr");
    prefs.remove("net_tx_power");
    prefs.remove("net_hop_limit");
    prefs.remove("net_tx_enabled");
    prefs.remove("net_override_duty");
    prefs.remove("net_channel_num");
    prefs.remove("net_freq_offset");
    prefs.remove("net_override_freq");
    prefs.remove("net_relay");
    prefs.remove("net_duty_cycle");
    prefs.remove("net_util");
    prefs.remove("mc_freq");
    prefs.remove("mc_bw");
    prefs.remove("mc_sf");
    prefs.remove("mc_cr");
    prefs.remove("mc_region_preset");
    prefs.remove("mc_tx_power");
    prefs.remove("mc_tx");
    prefs.remove("mc_repeat");
    prefs.remove("mc_rx_delay");
    prefs.remove("mc_airtime");
    prefs.remove("mc_flood_max");
    prefs.remove("mc_multi_acks");
    prefs.remove("mc_channel_slot");
    prefs.remove("mc_ch_slot");
    prefs.remove("needs_restart");
    prefs.end();

    build_item_list();
    ::ui::SystemNotification::show("Resetting...", 1500);
    delay(300);
    ESP.restart();
}

static void reset_node_db()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    app_ctx.clearNodeDb();
    {
        Preferences prefs;
        if (prefs.begin("chat_pki", false))
        {
            prefs.clear();
            prefs.end();
        }
    }
    ::ui::SystemNotification::show("Node DB reset", 3000);
}

static void clear_message_db()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    app_ctx.clearMessageDb();
    ::ui::SystemNotification::show("Message DB cleared", 3000);
}

static void settings_load()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    g_settings.chat_protocol = static_cast<int>(app_ctx.getConfig().mesh_protocol);
    g_settings.needs_restart = prefs_get_bool("needs_restart", false);

    if (kChatRegionOptionCount == 0)
    {
        size_t region_count = 0;
        const chat::meshtastic::RegionInfo* regions = chat::meshtastic::getRegionTable(&region_count);
        size_t limit = sizeof(kChatRegionOptions) / sizeof(kChatRegionOptions[0]);
        kChatRegionOptionCount = (region_count < limit) ? region_count : limit;
        for (size_t i = 0; i < kChatRegionOptionCount; ++i)
        {
            kChatRegionOptions[i].label = regions[i].label;
            kChatRegionOptions[i].value = regions[i].code;
        }
    }
    if (kMeshCoreRegionPresetOptionCount == 0)
    {
        size_t preset_count = 0;
        const chat::meshcore::RegionPreset* presets =
            chat::meshcore::getRegionPresetTable(&preset_count);
        size_t limit = sizeof(kMeshCoreRegionPresetOptions) / sizeof(kMeshCoreRegionPresetOptions[0]);
        if (limit > 0)
        {
            kMeshCoreRegionPresetOptions[0].label = "Custom";
            kMeshCoreRegionPresetOptions[0].value = 0;
            size_t copy_count = preset_count;
            if (copy_count > (limit - 1))
            {
                copy_count = limit - 1;
            }
            for (size_t i = 0; i < copy_count; ++i)
            {
                kMeshCoreRegionPresetOptions[i + 1].label = presets[i].title;
                kMeshCoreRegionPresetOptions[i + 1].value = presets[i].id;
            }
            kMeshCoreRegionPresetOptionCount = copy_count + 1;
        }
    }

    g_settings.gps_mode = prefs_get_int("gps_mode", 0);
    g_settings.gps_sat_mask = prefs_get_int("gps_sat_mask", 0x1 | 0x8 | 0x4);
    g_settings.gps_strategy = prefs_get_int("gps_strategy", 0);
    g_settings.gps_interval = prefs_get_int("gps_interval", 1);
    g_settings.gps_alt_ref = prefs_get_int("gps_alt_ref", 0);
    g_settings.gps_coord_format = prefs_get_int("gps_coord_fmt", 0);

    g_settings.map_coord_system = prefs_get_int("map_coord", 0);
    g_settings.map_source = prefs_get_int("map_source", 0);
    if (g_settings.map_source < 0 || g_settings.map_source > 2)
    {
        g_settings.map_source = 0;
    }
    g_settings.map_contour_enabled = prefs_get_bool("map_contour", false);
    g_settings.map_track_enabled = prefs_get_bool("map_track", false);
    g_settings.map_track_interval = prefs_get_int("map_track_interval", 1);
    g_settings.map_track_format = prefs_get_int("map_track_format", 0);

    app_ctx.getEffectiveUserInfo(g_settings.user_name,
                                 sizeof(g_settings.user_name),
                                 g_settings.short_name,
                                 sizeof(g_settings.short_name));
    const app::AppConfig& cfg = app_ctx.getConfig();
    const chat::MeshConfig& mt_cfg = cfg.meshtastic_config;
    const chat::MeshConfig& mc_cfg = cfg.meshcore_config;

    g_settings.chat_region = mt_cfg.region;
    g_settings.chat_channel = cfg.chat_channel;
    const uint8_t* active_psk =
        (cfg.mesh_protocol == chat::MeshProtocol::MeshCore) ? mc_cfg.secondary_key : mt_cfg.secondary_key;
    if (is_zero_key(active_psk, sizeof(mt_cfg.secondary_key)))
    {
        g_settings.chat_psk[0] = '\0';
    }
    else
    {
        bytes_to_hex(active_psk,
                     sizeof(mt_cfg.secondary_key),
                     g_settings.chat_psk,
                     sizeof(g_settings.chat_psk));
    }

    g_settings.net_use_preset = mt_cfg.use_preset;
    g_settings.net_modem_preset = mt_cfg.modem_preset;
    g_settings.net_manual_bw = static_cast<int>(std::lround(mt_cfg.bandwidth_khz));
    g_settings.net_manual_sf = mt_cfg.spread_factor;
    g_settings.net_manual_cr = mt_cfg.coding_rate;
    int tx_power = mt_cfg.tx_power;
    if (tx_power < kNetTxPowerMin) tx_power = kNetTxPowerMin;
    if (tx_power > kNetTxPowerMax) tx_power = kNetTxPowerMax;
    g_settings.net_tx_power = tx_power;
    g_settings.net_hop_limit = mt_cfg.hop_limit;
    g_settings.net_tx_enabled = (cfg.mesh_protocol == chat::MeshProtocol::MeshCore)
                                    ? mc_cfg.tx_enabled
                                    : mt_cfg.tx_enabled;
    g_settings.net_override_duty_cycle = mt_cfg.override_duty_cycle;
    g_settings.net_channel_num = mt_cfg.channel_num;
    g_settings.net_relay = mt_cfg.enable_relay;
    float_to_text(mt_cfg.frequency_offset_mhz, g_settings.net_freq_offset, sizeof(g_settings.net_freq_offset), 3);
    float_to_text(mt_cfg.override_frequency_mhz, g_settings.net_override_freq, sizeof(g_settings.net_override_freq), 3);
    g_settings.net_duty_cycle = cfg.net_duty_cycle;
    g_settings.net_channel_util = cfg.net_channel_util;

    if (chat::meshcore::isValidRegionPresetId(mc_cfg.meshcore_region_preset))
    {
        g_settings.mc_region_preset = mc_cfg.meshcore_region_preset;
    }
    else
    {
        g_settings.mc_region_preset = 0;
    }
    float_to_text(mc_cfg.meshcore_freq_mhz, g_settings.mc_freq, sizeof(g_settings.mc_freq), 3);
    float_to_text(mc_cfg.meshcore_bw_khz, g_settings.mc_bw, sizeof(g_settings.mc_bw), 3);
    g_settings.mc_sf = mc_cfg.meshcore_sf;
    g_settings.mc_cr = mc_cfg.meshcore_cr;
    g_settings.mc_tx_power = mc_cfg.tx_power;
    g_settings.mc_client_repeat = mc_cfg.meshcore_client_repeat;
    float_to_text(mc_cfg.meshcore_rx_delay_base, g_settings.mc_rx_delay, sizeof(g_settings.mc_rx_delay), 3);
    float_to_text(mc_cfg.meshcore_airtime_factor, g_settings.mc_airtime, sizeof(g_settings.mc_airtime), 3);
    g_settings.mc_flood_max = mc_cfg.meshcore_flood_max;
    g_settings.mc_multi_acks = mc_cfg.meshcore_multi_acks;
    g_settings.mc_channel_slot = mc_cfg.meshcore_channel_slot;
    strncpy(g_settings.mc_channel_name, mc_cfg.meshcore_channel_name, sizeof(g_settings.mc_channel_name) - 1);
    g_settings.mc_channel_name[sizeof(g_settings.mc_channel_name) - 1] = '\0';
    if (is_zero_key(mc_cfg.secondary_key, sizeof(mc_cfg.secondary_key)))
    {
        g_settings.mc_channel_key[0] = '\0';
    }
    else
    {
        bytes_to_hex(mc_cfg.secondary_key, sizeof(mc_cfg.secondary_key),
                     g_settings.mc_channel_key, sizeof(g_settings.mc_channel_key));
    }

    g_settings.privacy_encrypt_mode = cfg.privacy_encrypt_mode;
    g_settings.privacy_pki = cfg.privacy_pki;
    g_settings.privacy_nmea_output = cfg.privacy_nmea_output;
    g_settings.privacy_nmea_sentence = cfg.privacy_nmea_sentence;

    g_settings.screen_timeout_ms = prefs_get_int("screen_timeout", static_cast<int>(getScreenSleepTimeout()));
    g_settings.timezone_offset_min = prefs_get_int("timezone_offset", 0);
    g_settings.speaker_volume = prefs_get_int("speaker_volume",
                                              static_cast<int>(board.getMessageToneVolume()));
    if (g_settings.speaker_volume < 0)
    {
        g_settings.speaker_volume = 0;
    }
    if (g_settings.speaker_volume > 100)
    {
        g_settings.speaker_volume = 100;
    }
    board.setMessageToneVolume(static_cast<uint8_t>(g_settings.speaker_volume));

    g_settings.advanced_debug_logs = prefs_get_bool("adv_debug", false);
}

static void format_value(const settings::ui::SettingItem& item, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    switch (item.type)
    {
    case settings::ui::SettingType::Toggle:
        snprintf(out, out_len, "%s", (item.bool_value && *item.bool_value) ? "ON" : "OFF");
        break;
    case settings::ui::SettingType::Enum:
    {
        int value = item.enum_value ? *item.enum_value : 0;
        const char* label = "N/A";
        for (size_t i = 0; i < item.option_count; ++i)
        {
            if (item.options[i].value == value)
            {
                label = item.options[i].label;
                break;
            }
        }
        snprintf(out, out_len, "%s", label);
        break;
    }
    case settings::ui::SettingType::Text:
        if (item.text_value && item.text_value[0] != '\0')
        {
            if (item.mask_text)
            {
                snprintf(out, out_len, "****");
            }
            else
            {
                snprintf(out, out_len, "%s", item.text_value);
            }
        }
        else
        {
            snprintf(out, out_len, "Not set");
        }
        break;
    case settings::ui::SettingType::Action:
        snprintf(out, out_len, "Run");
        break;
    }
}

static void update_item_value(settings::ui::ItemWidget& widget)
{
    if (!widget.value_label || !widget.def)
    {
        return;
    }
    char value[48];
    format_value(*widget.def, value, sizeof(value));
    lv_label_set_text(widget.value_label, value);
}

static void modal_prepare_group()
{
    if (g_state.modal_group)
    {
        return;
    }
    s_modal_prev_group = settings::ui::input::get_group();
    g_state.modal_group = lv_group_create();
    set_default_group(g_state.modal_group);
}

static void modal_restore_group()
{
    if (g_state.modal_group)
    {
        lv_group_del(g_state.modal_group);
        g_state.modal_group = nullptr;
    }
    if (s_modal_prev_group)
    {
        set_default_group(s_modal_prev_group);
    }
    settings::ui::input::on_ui_refreshed();
}

static void modal_close()
{
    if (g_state.modal_root)
    {
        lv_obj_del_async(g_state.modal_root);
        g_state.modal_root = nullptr;
    }
    g_state.modal_textarea = nullptr;
    g_state.modal_error = nullptr;
    g_state.editing_item = nullptr;
    g_state.editing_widget = nullptr;
    s_option_click_count = 0;
    modal_restore_group();
}

static lv_obj_t* create_modal_root(lv_coord_t width, lv_coord_t height)
{
    lv_obj_t* bg = lv_obj_create(g_state.root);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    style::apply_modal_bg(bg);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, width, height);
    lv_obj_center(win);
    style::apply_modal_panel(win);
    lv_obj_set_style_pad_all(win, 8, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

static void on_text_save_clicked(lv_event_t* e)
{
    (void)e;
    if (!g_state.editing_item || !g_state.modal_textarea || !g_state.editing_widget)
    {
        modal_close();
        return;
    }
    const char* text = lv_textarea_get_text(g_state.modal_textarea);
    if (g_state.editing_item->text_value && g_state.editing_item->text_max > 0)
    {
        strncpy(g_state.editing_item->text_value, text, g_state.editing_item->text_max - 1);
        g_state.editing_item->text_value[g_state.editing_item->text_max - 1] = '\0';
        bool is_user_name = (g_state.editing_item->pref_key &&
                             strcmp(g_state.editing_item->pref_key, "chat_user") == 0);
        bool is_short_name = (g_state.editing_item->pref_key &&
                              strcmp(g_state.editing_item->pref_key, "chat_short") == 0);
        update_item_value(*g_state.editing_widget);
        bool broadcast_nodeinfo = false;
        if (is_user_name)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            strncpy(app_ctx.getConfig().node_name, g_state.editing_item->text_value,
                    sizeof(app_ctx.getConfig().node_name) - 1);
            app_ctx.getConfig().node_name[sizeof(app_ctx.getConfig().node_name) - 1] = '\0';
            app_ctx.saveConfig();
            app_ctx.applyUserInfo();
            broadcast_nodeinfo = true;
        }
        if (is_short_name)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            strncpy(app_ctx.getConfig().short_name, g_state.editing_item->text_value,
                    sizeof(app_ctx.getConfig().short_name) - 1);
            app_ctx.getConfig().short_name[sizeof(app_ctx.getConfig().short_name) - 1] = '\0';
            app_ctx.saveConfig();
            app_ctx.applyUserInfo();
            broadcast_nodeinfo = true;
        }
        if (broadcast_nodeinfo)
        {
            app::AppContext::getInstance().broadcastNodeInfo();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "chat_psk") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            uint8_t key[16] = {};
            if (!parse_psk(g_state.editing_item->text_value, key, sizeof(key)))
            {
                ::ui::SystemNotification::show("PSK must be 32 hex or 16 chars", 4000);
                modal_close();
                return;
            }
            if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::MeshCore)
            {
                memcpy(app_ctx.getConfig().meshcore_config.secondary_key, key, sizeof(key));
            }
            else
            {
                memcpy(app_ctx.getConfig().meshtastic_config.secondary_key, key, sizeof(key));
            }
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "net_freq_offset") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show("Invalid frequency offset", 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshtastic_config.frequency_offset_mhz = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "net_override_freq") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show("Invalid frequency value", 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshtastic_config.override_frequency_mhz = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_freq") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show("Invalid MeshCore frequency", 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_freq_mhz = value;
            app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
            g_settings.mc_region_preset = 0;
            prefs_put_int("mc_region_preset", 0);
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_bw") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show("Invalid MeshCore bandwidth", 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_bw_khz = value;
            app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
            g_settings.mc_region_preset = 0;
            prefs_put_int("mc_region_preset", 0);
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_rx_delay") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show("Invalid RX delay", 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_rx_delay_base = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_airtime") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show("Invalid airtime factor", 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_airtime_factor = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_channel_name") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            strncpy(app_ctx.getConfig().meshcore_config.meshcore_channel_name,
                    g_state.editing_item->text_value,
                    sizeof(app_ctx.getConfig().meshcore_config.meshcore_channel_name) - 1);
            app_ctx.getConfig().meshcore_config.meshcore_channel_name[sizeof(app_ctx.getConfig().meshcore_config.meshcore_channel_name) - 1] = '\0';
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_channel_key") == 0)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            uint8_t key[16] = {};
            if (!parse_psk(g_state.editing_item->text_value, key, sizeof(key)))
            {
                ::ui::SystemNotification::show("Key must be 32 hex or 16 chars", 3000);
                modal_close();
                return;
            }
            memcpy(app_ctx.getConfig().meshcore_config.secondary_key, key, sizeof(key));
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
    }
    modal_close();
}

static void on_text_cancel_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
}

static void open_text_modal(const settings::ui::SettingItem& item, settings::ui::ItemWidget& widget)
{
    if (g_state.modal_root)
    {
        return;
    }
    modal_prepare_group();
    g_state.modal_root = create_modal_root(300, 170);
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);

    lv_obj_t* title = lv_label_create(win);
    lv_label_set_text(title, item.label);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    g_state.modal_textarea = lv_textarea_create(win);
    lv_textarea_set_one_line(g_state.modal_textarea, true);
    lv_textarea_set_max_length(g_state.modal_textarea, static_cast<uint16_t>(item.text_max - 1));
    if (item.mask_text)
    {
        lv_textarea_set_password_mode(g_state.modal_textarea, true);
    }
    lv_obj_set_width(g_state.modal_textarea, LV_PCT(100));
    lv_obj_align(g_state.modal_textarea, LV_ALIGN_TOP_MID, 0, 28);
    if (item.text_value)
    {
        lv_textarea_set_text(g_state.modal_textarea, item.text_value);
        lv_textarea_set_cursor_pos(g_state.modal_textarea, LV_TEXTAREA_CURSOR_LAST);
    }

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, 90, 28);
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, on_text_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 90, 28);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_text_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    g_state.editing_item = &item;
    g_state.editing_widget = &widget;

    lv_group_add_obj(g_state.modal_group, g_state.modal_textarea);
    lv_group_add_obj(g_state.modal_group, save_btn);
    lv_group_add_obj(g_state.modal_group, cancel_btn);
    lv_group_focus_obj(g_state.modal_textarea);
}

static void on_option_clicked(lv_event_t* e)
{
    OptionClick* payload = static_cast<OptionClick*>(lv_event_get_user_data(e));
    if (!payload || !payload->item || !payload->item->enum_value)
    {
        return;
    }
    bool restart_now = false;
    bool rebuild_list = false;
    int previous_value = *payload->item->enum_value;
    *payload->item->enum_value = payload->value;
    prefs_put_int(payload->item->pref_key, payload->value);
    update_item_value(*payload->widget);
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mesh_protocol") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().mesh_protocol = static_cast<chat::MeshProtocol>(payload->value);
        app_ctx.saveConfig();
        restart_now = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "chat_region") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        chat::MeshConfig& mt_cfg = app_ctx.getConfig().meshtastic_config;
        mt_cfg.region = static_cast<uint8_t>(payload->value);
        const auto* region = chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(mt_cfg.region));
        if (region && region->power_limit_dbm > 0)
        {
            int8_t limit = static_cast<int8_t>(region->power_limit_dbm);
            if (mt_cfg.tx_power == 0 || mt_cfg.tx_power > limit)
            {
                mt_cfg.tx_power = limit;
            }
            if (g_settings.net_tx_power > limit)
            {
                g_settings.net_tx_power = limit;
            }
        }
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        app_ctx.applyNetworkLimits();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_use_preset") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.use_preset = (payload->value != 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_preset") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.modem_preset = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshtastic_config.use_preset = true;
        g_settings.net_use_preset = true;
        prefs_put_int("net_use_preset", 1);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_bw") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.bandwidth_khz = static_cast<float>(payload->value);
        app_ctx.getConfig().meshtastic_config.use_preset = false;
        g_settings.net_use_preset = false;
        prefs_put_int("net_use_preset", 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_sf") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.spread_factor = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshtastic_config.use_preset = false;
        g_settings.net_use_preset = false;
        prefs_put_int("net_use_preset", 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_cr") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.coding_rate = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshtastic_config.use_preset = false;
        g_settings.net_use_preset = false;
        prefs_put_int("net_use_preset", 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_hop_limit") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.hop_limit = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_channel_num") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.channel_num = static_cast<uint16_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "screen_timeout") == 0)
    {
        setScreenSleepTimeout(static_cast<uint32_t>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "speaker_volume") == 0)
    {
        const uint8_t volume = static_cast<uint8_t>(payload->value);
        board.setMessageToneVolume(volume);
        if (volume > 0)
        {
            // Immediate audible feedback so user can tune the level interactively.
            board.playMessageTone();
        }
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_interval") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        uint32_t interval_ms = static_cast<uint32_t>(payload->value) * 1000u;
        app_ctx.getConfig().gps_interval_ms = interval_ms;
        app_ctx.saveConfig();
        gps::gps_set_collection_interval(interval_ms);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_mode") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().gps_mode = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps::gps_set_gnss_config(app_ctx.getConfig().gps_mode, app_ctx.getConfig().gps_sat_mask);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_sat_mask") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().gps_sat_mask = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps::gps_set_gnss_config(app_ctx.getConfig().gps_mode, app_ctx.getConfig().gps_sat_mask);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_strategy") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().gps_strategy = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps::gps_set_power_strategy(static_cast<uint8_t>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_alt_ref") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().gps_alt_ref = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_coord_fmt") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().gps_coord_format = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_coord") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().map_coord_system = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_source") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        int source = payload->value;
        if (source < 0 || source > 2)
        {
            source = 0;
        }
        app_ctx.getConfig().map_source = static_cast<uint8_t>(source);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_track_interval") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().map_track_interval = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        auto& recorder = gps::TrackRecorder::getInstance();
        if (payload->value == 99)
        {
            recorder.setDistanceOnly(true);
            recorder.setIntervalSeconds(0);
        }
        else
        {
            recorder.setDistanceOnly(false);
            recorder.setIntervalSeconds(static_cast<uint32_t>(payload->value));
        }
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_track_format") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().map_track_format = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps::TrackRecorder::getInstance().setFormat(static_cast<gps::TrackFormat>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "chat_channel") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().chat_channel = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyChatDefaults();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_util") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().net_channel_util = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyNetworkLimits();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_tx_power") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshtastic_config.tx_power = static_cast<int8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_region_preset") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        chat::MeshConfig& mc_cfg = app_ctx.getConfig().meshcore_config;
        uint8_t preset_id = static_cast<uint8_t>(payload->value);
        if (!chat::meshcore::isValidRegionPresetId(preset_id))
        {
            preset_id = 0;
        }
        mc_cfg.meshcore_region_preset = preset_id;
        g_settings.mc_region_preset = preset_id;
        prefs_put_int("mc_region_preset", preset_id);
        if (preset_id > 0)
        {
            const chat::meshcore::RegionPreset* preset = chat::meshcore::findRegionPresetById(preset_id);
            if (preset)
            {
                mc_cfg.meshcore_freq_mhz = preset->freq_mhz;
                mc_cfg.meshcore_bw_khz = preset->bw_khz;
                mc_cfg.meshcore_sf = preset->sf;
                mc_cfg.meshcore_cr = preset->cr;
                float_to_text(mc_cfg.meshcore_freq_mhz, g_settings.mc_freq, sizeof(g_settings.mc_freq), 3);
                float_to_text(mc_cfg.meshcore_bw_khz, g_settings.mc_bw, sizeof(g_settings.mc_bw), 3);
                g_settings.mc_sf = mc_cfg.meshcore_sf;
                g_settings.mc_cr = mc_cfg.meshcore_cr;
            }
        }
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_sf") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshcore_config.meshcore_sf = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
        g_settings.mc_region_preset = 0;
        prefs_put_int("mc_region_preset", 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_cr") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshcore_config.meshcore_cr = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
        g_settings.mc_region_preset = 0;
        prefs_put_int("mc_region_preset", 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_tx_power") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshcore_config.tx_power = static_cast<int8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_flood_max") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshcore_config.meshcore_flood_max = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_channel_slot") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "privacy_encrypt") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().privacy_encrypt_mode = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyPrivacyConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "privacy_nmea") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().privacy_nmea_output = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps::gps_set_nmea_config(app_ctx.getConfig().privacy_nmea_output,
                                 app_ctx.getConfig().privacy_nmea_sentence);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "privacy_nmea_sent") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().privacy_nmea_sentence = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps::gps_set_nmea_config(app_ctx.getConfig().privacy_nmea_output,
                                 app_ctx.getConfig().privacy_nmea_sentence);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "timezone_offset") == 0)
    {
        ui_set_timezone_offset_min(payload->value);
        (void)previous_value;
        restart_now = true;
    }
    modal_close();
    if (rebuild_list)
    {
        build_item_list();
    }
    if (restart_now)
    {
        ::ui::SystemNotification::show("Restarting...", 1500);
        delay(300);
        ESP.restart();
    }
}

static void open_option_modal(const settings::ui::SettingItem& item, settings::ui::ItemWidget& widget)
{
    if (g_state.modal_root)
    {
        return;
    }
    modal_prepare_group();
    g_state.modal_root = create_modal_root(280, 200);
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);

    lv_obj_t* title = lv_label_create(win);
    lv_label_set_text(title, item.label);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* list = lv_obj_create(win);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    s_option_click_count = 0;
    for (size_t i = 0; i < item.option_count && s_option_click_count < kMaxOptions; ++i)
    {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), 28);
        style::apply_btn_modal(btn);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, item.options[i].label);
        style::apply_label_primary(label);
        lv_obj_center(label);

        s_option_clicks[s_option_click_count] = {&item, item.options[i].value, &widget};
        lv_obj_add_event_cb(btn, on_option_clicked, LV_EVENT_CLICKED,
                            &s_option_clicks[s_option_click_count]);
        if (item.enum_value && item.options[i].value == *item.enum_value)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        lv_group_add_obj(g_state.modal_group, btn);
        s_option_click_count++;
    }
    if (s_option_click_count > 0)
    {
        lv_group_focus_obj(lv_obj_get_child(list, 0));
    }
}

static void on_item_clicked(lv_event_t* e);
static void on_filter_clicked(lv_event_t* e);
static void on_filter_focused(lv_event_t* e);
static void apply_pending_category_cb(void* user_data);
static void on_list_back_clicked(lv_event_t* e);

// Options
static const settings::ui::SettingOption kGpsModeOptions[] = {
    {"High Accuracy", 0},
    {"Power Save", 1},
    {"Fix Only", 2},
};
static const settings::ui::SettingOption kGpsSatOptions[] = {
    {"GPS+BDS+GAL", 0x1 | 0x8 | 0x4},
    {"GPS", 0x1},
    {"GPS+BDS", 0x1 | 0x8},
    {"GPS+GAL", 0x1 | 0x4},
    {"GPS+BDS+GAL+GLO", 0x1 | 0x8 | 0x4 | 0x2},
};
static const settings::ui::SettingOption kGpsStrategyOptions[] = {
    {"Continuous", 0},
    {"Motion Wake", 1},
    {"Low Power Off", 2},
};
static const settings::ui::SettingOption kGpsIntervalOptions[] = {
    {"1s", 1},
    {"2s", 2},
    {"5s", 5},
    {"10s", 10},
};
static const settings::ui::SettingOption kGpsAltOptions[] = {
    {"Sea Level", 0},
    {"Ellipsoid", 1},
};
static const settings::ui::SettingOption kGpsCoordOptions[] = {
    {"DD", 0},
    {"DMS", 1},
    {"UTM", 2},
};

static const settings::ui::SettingOption kMapCoordOptions[] = {
    {"WGS84", 0},
    {"GCJ-02", 1},
    {"BD-09", 2},
};
static const settings::ui::SettingOption kMapSourceOptions[] = {
    {"OSM", 0},
    {"Terrain", 1},
    {"Satellite", 2},
};
static const settings::ui::SettingOption kMapTrackIntervalOptions[] = {
    {"1s", 1},
    {"5s", 5},
    {"10s", 10},
    {"Distance", 99},
};
static const settings::ui::SettingOption kMapTrackFormatOptions[] = {
    {"GPX", 0},
    {"CSV", 1},
    {"Binary", 2},
};

static const settings::ui::SettingOption kChatChannelOptions[] = {
    {"Primary", 0},
    {"Secondary", 1},
};
static const settings::ui::SettingOption kChatProtocolOptions[] = {
    {"Meshtastic", static_cast<int>(chat::MeshProtocol::Meshtastic)},
    {"MeshCore", static_cast<int>(chat::MeshProtocol::MeshCore)},
};

static const settings::ui::SettingOption kNetPresetOptions[] = {
    {"LongFast", meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST},
    {"LongModerate", meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE},
    {"LongSlow", meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW},
    {"VeryLongSlow", meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW},
    {"MediumFast", meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST},
    {"MediumSlow", meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW},
    {"ShortFast", meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST},
    {"ShortSlow", meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW},
    {"ShortTurbo", meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO},
};
static const settings::ui::SettingOption kBoolOptions[] = {
    {"OFF", 0},
    {"ON", 1},
};
static const settings::ui::SettingOption kNetManualBwOptions[] = {
    {"125 kHz", 125},
    {"250 kHz", 250},
    {"406 kHz", 406},
    {"500 kHz", 500},
    {"813 kHz", 813},
    {"1625 kHz", 1625},
};
static const settings::ui::SettingOption kSfOptions[] = {
    {"SF5", 5},
    {"SF6", 6},
    {"SF7", 7},
    {"SF8", 8},
    {"SF9", 9},
    {"SF10", 10},
    {"SF11", 11},
    {"SF12", 12},
};
static const settings::ui::SettingOption kCrOptions[] = {
    {"4/5", 5},
    {"4/6", 6},
    {"4/7", 7},
    {"4/8", 8},
};
static const settings::ui::SettingOption kHopLimitOptions[] = {
    {"1 hop", 1},
    {"2 hops", 2},
    {"3 hops", 3},
    {"4 hops", 4},
    {"5 hops", 5},
    {"6 hops", 6},
    {"7 hops", 7},
};
static const settings::ui::SettingOption kChannelNumOptions[] = {
    {"Auto", 0},
    {"1", 1},
    {"2", 2},
    {"3", 3},
    {"4", 4},
    {"5", 5},
    {"6", 6},
    {"7", 7},
    {"8", 8},
    {"9", 9},
    {"10", 10},
    {"11", 11},
    {"12", 12},
    {"13", 13},
    {"14", 14},
    {"15", 15},
    {"16", 16},
};
static const settings::ui::SettingOption kMeshCoreChannelSlotOptions[] = {
    {"Auto", 0},
    {"1", 1},
    {"2", 2},
    {"3", 3},
    {"4", 4},
    {"5", 5},
    {"6", 6},
    {"7", 7},
    {"8", 8},
    {"9", 9},
    {"10", 10},
    {"11", 11},
    {"12", 12},
    {"13", 13},
    {"14", 14},
};
static const settings::ui::SettingOption kMeshCoreFloodOptions[] = {
    {"0", 0},
    {"8", 8},
    {"16", 16},
    {"24", 24},
    {"32", 32},
    {"48", 48},
    {"64", 64},
};
static const settings::ui::SettingOption kNetTxPowerOptions[] = {
    {"-9 dBm", -9},
    {"-8 dBm", -8},
    {"-7 dBm", -7},
    {"-6 dBm", -6},
    {"-5 dBm", -5},
    {"-4 dBm", -4},
    {"-3 dBm", -3},
    {"-2 dBm", -2},
    {"-1 dBm", -1},
    {"0 dBm", 0},
    {"1 dBm", 1},
    {"2 dBm", 2},
    {"3 dBm", 3},
    {"4 dBm", 4},
    {"5 dBm", 5},
    {"6 dBm", 6},
    {"7 dBm", 7},
    {"8 dBm", 8},
    {"9 dBm", 9},
    {"10 dBm", 10},
    {"11 dBm", 11},
    {"12 dBm", 12},
    {"13 dBm", 13},
    {"14 dBm", 14},
    {"15 dBm", 15},
    {"16 dBm", 16},
    {"17 dBm", 17},
    {"18 dBm", 18},
    {"19 dBm", 19},
    {"20 dBm", 20},
    {"21 dBm", 21},
    {"22 dBm", 22},
    {"23 dBm", 23},
    {"24 dBm", 24},
    {"25 dBm", 25},
    {"26 dBm", 26},
    {"27 dBm", 27},
    {"28 dBm", 28},
    {"29 dBm", 29},
    {"30 dBm", 30},
};
static const settings::ui::SettingOption kNetUtilOptions[] = {
    {"Auto", 0},
    {"Limit 25%", 25},
    {"Limit 50%", 50},
};

static const settings::ui::SettingOption kPrivacyEncryptOptions[] = {
    {"OFF", 0},
    {"PSK", 1},
    {"PKI", 2},
};
static const settings::ui::SettingOption kPrivacyNmeaOptions[] = {
    {"OFF", 0},
    {"1Hz", 1},
    {"5Hz", 5},
};
static const settings::ui::SettingOption kPrivacyNmeaSentenceOptions[] = {
    {"GGA+RMC+GSV", 0},
    {"RMC+GSV", 1},
    {"GGA+RMC", 2},
};

static const settings::ui::SettingOption kScreenTimeoutOptions[] = {
    {"15s", 15000},
    {"30s", 30000},
    {"1min", 60000},
    {"Always", 300000},
};

static const settings::ui::SettingOption kSpeakerVolumeOptions[] = {
    {"OFF", 0},
    {"30%", 30},
    {"45%", 45},
    {"60%", 60},
    {"75%", 75},
    {"90%", 90},
    {"100%", 100},
};

static const settings::ui::SettingOption kTimeZoneOptions[] = {
    {"UTC", 0},
    {"Beijing (UTC+8)", 480},
    {"Taipei (UTC+8)", 480},
    {"Hong Kong (UTC+8)", 480},
    {"Tokyo (UTC+9)", 540},
    {"Seoul (UTC+9)", 540},
    {"Singapore (UTC+8)", 480},
    {"Bangkok (UTC+7)", 420},
    {"Kolkata (UTC+5:30)", 330},
    {"Dubai (UTC+4)", 240},
    {"London (UTC+0 / DST)", 0},
    {"Berlin (UTC+1 / DST)", 60},
    {"Paris (UTC+1 / DST)", 60},
    {"Rome (UTC+1 / DST)", 60},
    {"Moscow (UTC+3)", 180},
    {"New York (UTC-5 / DST)", -300},
    {"Chicago (UTC-6 / DST)", -360},
    {"Denver (UTC-7 / DST)", -420},
    {"Los Angeles (UTC-8 / DST)", -480},
    {"Phoenix (UTC-7)", -420},
    {"Sao Paulo (UTC-3)", -180},
    {"Sydney (UTC+10 / DST)", 600},
    {"Melbourne (UTC+10 / DST)", 600},
    {"Auckland (UTC+12 / DST)", 720},
};

static settings::ui::SettingItem kGpsItems[] = {
    {"Location Mode", settings::ui::SettingType::Enum, kGpsModeOptions, 3, &g_settings.gps_mode, nullptr, nullptr, 0, false, "gps_mode"},
    {"Satellite Systems", settings::ui::SettingType::Enum, kGpsSatOptions, 5, &g_settings.gps_sat_mask, nullptr, nullptr, 0, false, "gps_sat_mask"},
    {"Position Strategy", settings::ui::SettingType::Enum, kGpsStrategyOptions, 3, &g_settings.gps_strategy, nullptr, nullptr, 0, false, "gps_strategy"},
    {"Update Interval", settings::ui::SettingType::Enum, kGpsIntervalOptions, 4, &g_settings.gps_interval, nullptr, nullptr, 0, false, "gps_interval"},
    {"Altitude Reference", settings::ui::SettingType::Enum, kGpsAltOptions, 2, &g_settings.gps_alt_ref, nullptr, nullptr, 0, false, "gps_alt_ref"},
    {"Coordinate Format", settings::ui::SettingType::Enum, kGpsCoordOptions, 3, &g_settings.gps_coord_format, nullptr, nullptr, 0, false, "gps_coord_fmt"},
    {"NMEA Output", settings::ui::SettingType::Enum, kPrivacyNmeaOptions, 3, &g_settings.privacy_nmea_output, nullptr, nullptr, 0, false, "privacy_nmea"},
    {"NMEA Sentences", settings::ui::SettingType::Enum, kPrivacyNmeaSentenceOptions, 3, &g_settings.privacy_nmea_sentence, nullptr, nullptr, 0, false, "privacy_nmea_sent"},
};

static settings::ui::SettingItem kMapItems[] = {
    {"Coordinate System", settings::ui::SettingType::Enum, kMapCoordOptions, 3, &g_settings.map_coord_system, nullptr, nullptr, 0, false, "map_coord"},
    {"Map Source", settings::ui::SettingType::Enum, kMapSourceOptions, 3, &g_settings.map_source, nullptr, nullptr, 0, false, "map_source"},
    {"Contour Overlay", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.map_contour_enabled, nullptr, 0, false, "map_contour"},
    {"Track Recording", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.map_track_enabled, nullptr, 0, false, "map_track"},
    {"Track Interval", settings::ui::SettingType::Enum, kMapTrackIntervalOptions, 4, &g_settings.map_track_interval, nullptr, nullptr, 0, false, "map_track_interval"},
    {"Track Format", settings::ui::SettingType::Enum, kMapTrackFormatOptions, 3, &g_settings.map_track_format, nullptr, nullptr, 0, false, "map_track_format"},
};

static settings::ui::SettingItem kChatItems[] = {
    {"User Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.user_name, sizeof(g_settings.user_name), false, "chat_user"},
    {"Short Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.short_name, sizeof(g_settings.short_name), false, "chat_short"},
    {"Protocol", settings::ui::SettingType::Enum, kChatProtocolOptions, 2, &g_settings.chat_protocol, nullptr, nullptr, 0, false, "mesh_protocol"},
    {"Region", settings::ui::SettingType::Enum, kChatRegionOptions, 0, &g_settings.chat_region, nullptr, nullptr, 0, false, "chat_region"},
    {"Channel", settings::ui::SettingType::Enum, kChatChannelOptions, 2, &g_settings.chat_channel, nullptr, nullptr, 0, false, "chat_channel"},
    {"Channel Key / PSK", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.chat_psk, sizeof(g_settings.chat_psk), true, "chat_psk"},
    {"Encryption Mode", settings::ui::SettingType::Enum, kPrivacyEncryptOptions, 3, &g_settings.privacy_encrypt_mode, nullptr, nullptr, 0, false, "privacy_encrypt"},
    {"PKI", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.privacy_pki, nullptr, 0, false, "privacy_pki"},
    {"Reset Mesh Profiles", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_reset_mesh"},
    {"Reset Node DB", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_reset_nodes"},
    {"Clear Message DB", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_clear_messages"},
};

static settings::ui::SettingItem kNetworkItems[] = {
    {"Use Preset", settings::ui::SettingType::Enum, kBoolOptions, sizeof(kBoolOptions) / sizeof(kBoolOptions[0]), &g_settings.net_use_preset, nullptr, nullptr, 0, false, "net_use_preset"},
    {"Modem Preset", settings::ui::SettingType::Enum, kNetPresetOptions, sizeof(kNetPresetOptions) / sizeof(kNetPresetOptions[0]), &g_settings.net_modem_preset, nullptr, nullptr, 0, false, "net_preset"},
    {"Manual BW", settings::ui::SettingType::Enum, kNetManualBwOptions, sizeof(kNetManualBwOptions) / sizeof(kNetManualBwOptions[0]), &g_settings.net_manual_bw, nullptr, nullptr, 0, false, "net_bw"},
    {"Manual SF", settings::ui::SettingType::Enum, kSfOptions, sizeof(kSfOptions) / sizeof(kSfOptions[0]), &g_settings.net_manual_sf, nullptr, nullptr, 0, false, "net_sf"},
    {"Manual CR", settings::ui::SettingType::Enum, kCrOptions, sizeof(kCrOptions) / sizeof(kCrOptions[0]), &g_settings.net_manual_cr, nullptr, nullptr, 0, false, "net_cr"},
    {"TX Power", settings::ui::SettingType::Enum, kNetTxPowerOptions,
     sizeof(kNetTxPowerOptions) / sizeof(kNetTxPowerOptions[0]), &g_settings.net_tx_power, nullptr, nullptr, 0, false, "net_tx_power"},
    {"Hop Limit", settings::ui::SettingType::Enum, kHopLimitOptions, sizeof(kHopLimitOptions) / sizeof(kHopLimitOptions[0]), &g_settings.net_hop_limit, nullptr, nullptr, 0, false, "net_hop_limit"},
    {"TX Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_tx_enabled, nullptr, 0, false, "net_tx_enabled"},
    {"Override Duty Cycle", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_override_duty_cycle, nullptr, 0, false, "net_override_duty"},
    {"Channel Slot", settings::ui::SettingType::Enum, kChannelNumOptions, sizeof(kChannelNumOptions) / sizeof(kChannelNumOptions[0]), &g_settings.net_channel_num, nullptr, nullptr, 0, false, "net_channel_num"},
    {"Freq Offset (MHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.net_freq_offset, sizeof(g_settings.net_freq_offset), false, "net_freq_offset"},
    {"Override Freq (MHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.net_override_freq, sizeof(g_settings.net_override_freq), false, "net_override_freq"},
    {"MC Region Preset", settings::ui::SettingType::Enum, kMeshCoreRegionPresetOptions, 0, &g_settings.mc_region_preset, nullptr, nullptr, 0, false, "mc_region_preset"},
    {"MC Frequency (MHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_freq, sizeof(g_settings.mc_freq), false, "mc_freq"},
    {"MC Bandwidth (kHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_bw, sizeof(g_settings.mc_bw), false, "mc_bw"},
    {"MC Spread Factor", settings::ui::SettingType::Enum, kSfOptions, sizeof(kSfOptions) / sizeof(kSfOptions[0]), &g_settings.mc_sf, nullptr, nullptr, 0, false, "mc_sf"},
    {"MC Coding Rate", settings::ui::SettingType::Enum, kCrOptions, sizeof(kCrOptions) / sizeof(kCrOptions[0]), &g_settings.mc_cr, nullptr, nullptr, 0, false, "mc_cr"},
    {"MC TX Power", settings::ui::SettingType::Enum, kNetTxPowerOptions, sizeof(kNetTxPowerOptions) / sizeof(kNetTxPowerOptions[0]), &g_settings.mc_tx_power, nullptr, nullptr, 0, false, "mc_tx_power"},
    {"MC Repeat", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_client_repeat, nullptr, 0, false, "mc_repeat"},
    {"MC RX Delay Base", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_rx_delay, sizeof(g_settings.mc_rx_delay), false, "mc_rx_delay"},
    {"MC Airtime Factor", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_airtime, sizeof(g_settings.mc_airtime), false, "mc_airtime"},
    {"MC Flood Max", settings::ui::SettingType::Enum, kMeshCoreFloodOptions, sizeof(kMeshCoreFloodOptions) / sizeof(kMeshCoreFloodOptions[0]), &g_settings.mc_flood_max, nullptr, nullptr, 0, false, "mc_flood_max"},
    {"MC Multi ACKs", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_multi_acks, nullptr, 0, false, "mc_multi_acks"},
    {"MC Channel Slot", settings::ui::SettingType::Enum, kMeshCoreChannelSlotOptions, sizeof(kMeshCoreChannelSlotOptions) / sizeof(kMeshCoreChannelSlotOptions[0]), &g_settings.mc_channel_slot, nullptr, nullptr, 0, false, "mc_channel_slot"},
    {"MC Channel Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_channel_name, sizeof(g_settings.mc_channel_name), false, "mc_channel_name"},
    {"MC Channel Key", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_channel_key, sizeof(g_settings.mc_channel_key), true, "mc_channel_key"},
    {"Duty Cycle Limit", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_duty_cycle, nullptr, 0, false, "net_duty_cycle"},
    {"Channel Utilization", settings::ui::SettingType::Enum, kNetUtilOptions, 3, &g_settings.net_channel_util, nullptr, nullptr, 0, false, "net_util"},
};

static settings::ui::SettingItem kScreenItems[] = {
    {"Screen Timeout", settings::ui::SettingType::Enum, kScreenTimeoutOptions, 4, &g_settings.screen_timeout_ms, nullptr, nullptr, 0, false, "screen_timeout"},
    {"Speaker Volume", settings::ui::SettingType::Enum, kSpeakerVolumeOptions,
     sizeof(kSpeakerVolumeOptions) / sizeof(kSpeakerVolumeOptions[0]), &g_settings.speaker_volume, nullptr, nullptr, 0, false, "speaker_volume"},
    {"Time Zone", settings::ui::SettingType::Enum, kTimeZoneOptions, sizeof(kTimeZoneOptions) / sizeof(kTimeZoneOptions[0]), &g_settings.timezone_offset_min, nullptr, nullptr, 0, false, "timezone_offset"},
};

static settings::ui::SettingItem kAdvancedItems[] = {
    {"Debug Logs", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.advanced_debug_logs, nullptr, 0, false, "adv_debug"},
};

static const CategoryDef kCategories[] = {
    {"GPS", kGpsItems, sizeof(kGpsItems) / sizeof(kGpsItems[0])},
    {"Map", kMapItems, sizeof(kMapItems) / sizeof(kMapItems[0])},
    {"Chat", kChatItems, sizeof(kChatItems) / sizeof(kChatItems[0])},
    {"Network", kNetworkItems, sizeof(kNetworkItems) / sizeof(kNetworkItems[0])},
    {"System", kScreenItems, sizeof(kScreenItems) / sizeof(kScreenItems[0])},
    {"Advanced", kAdvancedItems, sizeof(kAdvancedItems) / sizeof(kAdvancedItems[0])},
};

static void update_filter_styles()
{
    for (size_t i = 0; i < g_state.filter_count; ++i)
    {
        if (!g_state.filter_buttons[i]) continue;
        if (static_cast<int>(i) == g_state.current_category)
        {
            lv_obj_add_state(g_state.filter_buttons[i], LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(g_state.filter_buttons[i], LV_STATE_CHECKED);
        }
    }
}

static bool has_pref_key(const settings::ui::SettingItem& item, const char* key)
{
    return item.pref_key && key && strcmp(item.pref_key, key) == 0;
}

static bool should_show_item(const settings::ui::SettingItem& item)
{
    if (!item.pref_key)
    {
        return true;
    }

    const bool meshcore = (g_settings.chat_protocol == static_cast<int>(chat::MeshProtocol::MeshCore));

    // Relay is currently not implemented as real forwarding in Meshtastic path.
    if (has_pref_key(item, "net_relay"))
    {
        return false;
    }

    if (meshcore)
    {
        if (has_pref_key(item, "chat_region")) return false;
        if (has_pref_key(item, "chat_channel")) return false;
        if (has_pref_key(item, "chat_psk")) return false;
        if (has_pref_key(item, "privacy_pki")) return false;

        if (has_pref_key(item, "net_use_preset")) return false;
        if (has_pref_key(item, "net_preset")) return false;
        if (has_pref_key(item, "net_bw")) return false;
        if (has_pref_key(item, "net_sf")) return false;
        if (has_pref_key(item, "net_cr")) return false;
        if (has_pref_key(item, "net_tx_power")) return false;
        if (has_pref_key(item, "net_hop_limit")) return false;
        if (has_pref_key(item, "net_override_duty")) return false;
        if (has_pref_key(item, "net_channel_num")) return false;
        if (has_pref_key(item, "net_freq_offset")) return false;
        if (has_pref_key(item, "net_override_freq")) return false;
    }
    else
    {
        if (has_pref_key(item, "mc_region_preset")) return false;
        if (has_pref_key(item, "mc_freq")) return false;
        if (has_pref_key(item, "mc_bw")) return false;
        if (has_pref_key(item, "mc_sf")) return false;
        if (has_pref_key(item, "mc_cr")) return false;
        if (has_pref_key(item, "mc_tx_power")) return false;
        if (has_pref_key(item, "mc_repeat")) return false;
        if (has_pref_key(item, "mc_rx_delay")) return false;
        if (has_pref_key(item, "mc_airtime")) return false;
        if (has_pref_key(item, "mc_flood_max")) return false;
        if (has_pref_key(item, "mc_multi_acks")) return false;
        if (has_pref_key(item, "mc_channel_slot")) return false;
        if (has_pref_key(item, "mc_channel_name")) return false;
        if (has_pref_key(item, "mc_channel_key")) return false;

        if (has_pref_key(item, "net_preset"))
        {
            return g_settings.net_use_preset;
        }
        if (has_pref_key(item, "net_bw") || has_pref_key(item, "net_sf") || has_pref_key(item, "net_cr"))
        {
            return !g_settings.net_use_preset;
        }
    }

    return true;
}

static void build_item_list()
{
    if (!g_state.list_panel) return;
    if (s_building_list)
    {
        return;
    }
    s_building_list = true;
    g_state.list_back_btn = nullptr;
    lv_obj_clean(g_state.list_panel);
    g_state.item_count = 0;
    lv_obj_clear_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);

    const CategoryDef& cat = kCategories[g_state.current_category];
    for (size_t i = 0; i < cat.item_count && g_state.item_count < kMaxItems; ++i)
    {
        settings::ui::ItemWidget& widget = g_state.item_widgets[g_state.item_count];
        widget.def = &cat.items[i];
        if (widget.def && widget.def->pref_key && strcmp(widget.def->pref_key, "chat_region") == 0)
        {
            const_cast<settings::ui::SettingItem*>(widget.def)->option_count = kChatRegionOptionCount;
        }
        if (widget.def && widget.def->pref_key && strcmp(widget.def->pref_key, "mc_region_preset") == 0)
        {
            const_cast<settings::ui::SettingItem*>(widget.def)->option_count = kMeshCoreRegionPresetOptionCount;
        }
        if (!should_show_item(*widget.def))
        {
            continue;
        }

        lv_obj_t* btn = lv_btn_create(g_state.list_panel);
        lv_obj_set_size(btn, LV_PCT(100), 28);
        lv_obj_set_style_pad_left(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 10, LV_PART_MAIN);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style::apply_list_item(btn);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, widget.def->label);
        style::apply_label_primary(label);

        widget.value_label = lv_label_create(btn);
        style::apply_label_muted(widget.value_label);
        update_item_value(widget);

        widget.btn = btn;
        lv_obj_add_event_cb(btn, on_item_clicked, LV_EVENT_CLICKED, &widget);
        g_state.item_count++;
    }
    g_state.list_back_btn = lv_btn_create(g_state.list_panel);
    lv_obj_set_size(g_state.list_back_btn, LV_PCT(100), 28);
    lv_obj_set_style_pad_left(g_state.list_back_btn, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_state.list_back_btn, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.list_back_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_state.list_back_btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style::apply_list_item(g_state.list_back_btn);
    lv_obj_t* back_label = lv_label_create(g_state.list_back_btn);
    lv_label_set_text(back_label, "Back");
    style::apply_label_primary(back_label);
    lv_obj_add_event_cb(g_state.list_back_btn, on_list_back_clicked, LV_EVENT_CLICKED, nullptr);
    settings::ui::input::on_ui_refreshed();
    lv_obj_scroll_to_y(g_state.list_panel, 0, LV_ANIM_OFF);
    lv_obj_invalidate(g_state.list_panel);
    lv_obj_add_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_state.list_panel, LV_SCROLLBAR_MODE_AUTO);
    s_building_list = false;
}

static void on_item_clicked(lv_event_t* e)
{
    settings::ui::ItemWidget* widget = static_cast<settings::ui::ItemWidget*>(lv_event_get_user_data(e));
    if (!widget || !widget->def) return;
    const SettingItem& item = *widget->def;
    if (item.type == settings::ui::SettingType::Toggle)
    {
        if (item.bool_value)
        {
            *item.bool_value = !(*item.bool_value);
            prefs_put_bool(item.pref_key, *item.bool_value);
            update_item_value(*widget);
            if (item.pref_key && strcmp(item.pref_key, "net_relay") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().meshtastic_config.enable_relay = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "map_track") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().map_track_enabled = *item.bool_value;
                app_ctx.saveConfig();
                gps::TrackRecorder::getInstance().setAutoRecording(*item.bool_value);
            }
            if (item.pref_key && strcmp(item.pref_key, "map_contour") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().map_contour_enabled = *item.bool_value;
                app_ctx.saveConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "net_duty_cycle") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().net_duty_cycle = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyNetworkLimits();
            }
            if (item.pref_key && strcmp(item.pref_key, "net_tx_enabled") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::MeshCore)
                {
                    app_ctx.getConfig().meshcore_config.tx_enabled = *item.bool_value;
                }
                else
                {
                    app_ctx.getConfig().meshtastic_config.tx_enabled = *item.bool_value;
                }
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "net_override_duty") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().meshtastic_config.override_duty_cycle = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
                app_ctx.applyNetworkLimits();
            }
            if (item.pref_key && strcmp(item.pref_key, "mc_repeat") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().meshcore_config.meshcore_client_repeat = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "mc_multi_acks") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().meshcore_config.meshcore_multi_acks = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "privacy_pki") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().privacy_pki = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyPrivacyConfig();
            }
        }
        return;
    }
    if (item.type == settings::ui::SettingType::Enum)
    {
        open_option_modal(item, *widget);
        return;
    }
    if (item.type == settings::ui::SettingType::Text)
    {
        open_text_modal(item, *widget);
        return;
    }
    if (item.type == settings::ui::SettingType::Action)
    {
        if (item.pref_key && strcmp(item.pref_key, "chat_reset_mesh") == 0)
        {
            reset_mesh_settings();
        }
        else if (item.pref_key && strcmp(item.pref_key, "chat_reset_nodes") == 0)
        {
            reset_node_db();
        }
        else if (item.pref_key && strcmp(item.pref_key, "chat_clear_messages") == 0)
        {
            clear_message_db();
        }
        return;
    }
}

static void on_filter_clicked(lv_event_t* e)
{
    intptr_t idx = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (idx < 0) return;
    if (s_building_list) return;
    g_state.current_category = static_cast<int>(idx);
    update_filter_styles();
    build_item_list();
    settings::ui::input::focus_to_list();
}

static void on_filter_focused(lv_event_t* e)
{
    intptr_t idx = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (idx < 0) return;
    if (s_building_list) return;
    s_pending_category = static_cast<int>(idx);
    if (!s_category_update_scheduled)
    {
        s_category_update_scheduled = true;
        lv_async_call(apply_pending_category_cb, nullptr);
    }
}

static void apply_pending_category_cb(void* /*user_data*/)
{
    s_category_update_scheduled = false;
    if (s_pending_category < 0)
    {
        return;
    }
    g_state.current_category = s_pending_category;
    s_pending_category = -1;
    lv_obj_clear_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_state.list_panel, LV_SCROLLBAR_MODE_OFF);
    update_filter_styles();
    build_item_list();
}

static void on_list_back_clicked(lv_event_t* /*e*/)
{
    settings::ui::input::focus_to_filter();
}

static void settings_back_cb(void* /*user_data*/)
{
    ui_request_exit_to_menu();
}

} // namespace

void create(lv_obj_t* parent)
{
    settings_load();

    // Avoid auto-adding widgets to the current default group during creation.
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    g_state.parent = parent;
    g_state.root = layout::create_root(parent);
    layout::create_header(g_state.root, settings_back_cb, nullptr);

    g_state.content = layout::create_content(g_state.root);
    layout::create_filter_panel(g_state.content);
    layout::create_list_panel(g_state.content);

    g_state.filter_count = sizeof(kCategories) / sizeof(kCategories[0]);
    for (size_t i = 0; i < g_state.filter_count; ++i)
    {
        lv_obj_t* btn = lv_btn_create(g_state.filter_panel);
        lv_obj_set_size(btn, LV_PCT(100), 28);
        style::apply_btn_filter(btn);
        lv_obj_add_event_cb(btn, on_filter_clicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(btn, on_filter_focused, LV_EVENT_FOCUSED, reinterpret_cast<void*>(i));
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, kCategories[i].label);
        style::apply_label_primary(label);
        lv_obj_center(label);
        g_state.filter_buttons[i] = btn;
    }

    update_filter_styles();
    build_item_list();

    // Restore previous default group before initializing input.
    set_default_group(prev_group);
    settings::ui::input::init();
}

void destroy()
{
    if (g_state.modal_root)
    {
        modal_close();
    }
    settings::ui::input::cleanup();
    if (g_state.root)
    {
        lv_obj_del_async(g_state.root);
        g_state.root = nullptr;
    }
    if (g_state.parent)
    {
        lv_obj_invalidate(g_state.parent);
    }
    g_state = settings::ui::UiState{};
}

} // namespace settings::ui::components
