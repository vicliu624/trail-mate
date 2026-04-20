/**
 * @file settings_page_components.cpp
 * @brief Settings UI components implementation
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "board/BoardBase.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "meshtastic/config.pb.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/app_runtime.h"
#include "ui/components/info_card.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/page/page_profile.h"
#include "ui/screens/settings/settings_page_components.h"
#include "ui/screens/settings/settings_page_input.h"
#include "ui/screens/settings/settings_page_layout.h"
#include "ui/screens/settings/settings_page_styles.h"
#include "ui/screens/settings/settings_state.h"
#include "ui/screens/team/team_ui_store.h"
#include "ui/ui_common.h"
#include "ui/widgets/system_notification.h"
#include "ui/widgets/top_bar.h"

namespace settings::ui::components
{

namespace
{

namespace device_runtime = ::platform::ui::device;
namespace gps_runtime = ::platform::ui::gps;
namespace screen_runtime = ::platform::ui::screen;
namespace settings_store = ::platform::ui::settings_store;
namespace tracker_runtime = ::platform::ui::tracker;
namespace wifi_runtime = ::platform::ui::wifi;

constexpr size_t kMaxItems = 32;
constexpr size_t kMaxOptions = 40;
constexpr size_t kMaxWifiNetworks = 24;
constexpr const char* kPrefsNs = "settings";
constexpr int kNetTxPowerMin = app::AppConfig::kTxPowerMinDbm;
constexpr int kNetTxPowerMax = app::AppConfig::kTxPowerMaxDbm;

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
static settings::ui::SettingOption kTxPowerOptions[64] = {};
static size_t kTxPowerOptionCount = 0;
static char kTxPowerLabels[64][12] = {};
static settings::ui::SettingOption kLocaleOptions[16] = {};
static size_t kLocaleOptionCount = 0;
static char kLocaleOptionLabels[16][48] = {};
static settings::ui::SettingOption kWifiNetworkOptions[kMaxWifiNetworks] = {};
static size_t kWifiNetworkOptionCount = 0;
static char kWifiNetworkOptionLabels[kMaxWifiNetworks][64] = {};
static wifi_runtime::ScanResult kWifiScanResults[kMaxWifiNetworks] = {};

static void update_item_value(settings::ui::ItemWidget& widget);
static void open_factory_reset_modal();
static bool option_labels_are_translated(const settings::ui::SettingItem& item);
static bool option_labels_use_content_font(const settings::ui::SettingItem& item);

static void copy_bounded(char* out, size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

static void clear_wifi_scan_options()
{
    kWifiNetworkOptionCount = 0;
    for (size_t i = 0; i < kMaxWifiNetworks; ++i)
    {
        kWifiNetworkOptions[i] = settings::ui::SettingOption{};
        kWifiNetworkOptionLabels[i][0] = '\0';
        kWifiScanResults[i] = wifi_runtime::ScanResult{};
    }
    g_settings.wifi_network_index = -1;
}

static void rebuild_wifi_scan_options(const std::vector<wifi_runtime::ScanResult>& results)
{
    clear_wifi_scan_options();

    const size_t limit = results.size() < kMaxWifiNetworks ? results.size() : kMaxWifiNetworks;
    for (size_t i = 0; i < limit; ++i)
    {
        kWifiScanResults[i] = results[i];
        std::snprintf(kWifiNetworkOptionLabels[i],
                      sizeof(kWifiNetworkOptionLabels[i]),
                      "%s (%d dBm%s)",
                      results[i].ssid,
                      results[i].rssi,
                      results[i].requires_password ? ", lock" : "");
        kWifiNetworkOptions[i].label = kWifiNetworkOptionLabels[i];
        kWifiNetworkOptions[i].value = static_cast<int>(i);
    }
    kWifiNetworkOptionCount = limit;
}

static void refresh_wifi_state_from_runtime()
{
    wifi_runtime::Config config{};
    (void)wifi_runtime::load_config(config);

    const wifi_runtime::Status status = wifi_runtime::status();
    g_settings.wifi_enabled = config.enabled;
    copy_bounded(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), config.ssid);
    copy_bounded(g_settings.wifi_password, sizeof(g_settings.wifi_password), config.password);
    if (!status.supported && status.message[0] == '\0')
    {
        copy_bounded(g_settings.wifi_status, sizeof(g_settings.wifi_status), "Wi-Fi unsupported");
    }
    else
    {
        copy_bounded(g_settings.wifi_status, sizeof(g_settings.wifi_status), status.message);
    }
}

static bool use_tdeck_info_card_layout()
{
    return ::ui::components::info_card::use_tdeck_layout();
}

static constexpr bool use_touch_first_settings_mode()
{
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    return true;
#else
    return false;
#endif
}

static bool should_show_settings_list_back_button()
{
    return !use_touch_first_settings_mode();
}

static lv_coord_t resolve_settings_list_item_height()
{
    const lv_coord_t base = ::ui::page_profile::resolve_control_button_height();
    if (!use_tdeck_info_card_layout())
    {
        return base;
    }

    return ::ui::components::info_card::resolve_height(base);
}

static void configure_list_item_button(lv_obj_t* btn)
{
    if (use_tdeck_info_card_layout())
    {
        ::ui::components::info_card::configure_item(
            btn, ::ui::page_profile::resolve_control_button_height());
        return;
    }

    lv_obj_set_size(btn, LV_PCT(100), resolve_settings_list_item_height());
    lv_obj_set_style_pad_left(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void create_item_content(settings::ui::ItemWidget& widget, lv_obj_t* btn)
{
    if (use_tdeck_info_card_layout())
    {
        const auto slots = ::ui::components::info_card::create_content(btn);
        ::ui::i18n::set_label_text(slots.header_main_label, widget.def->label);
        style::apply_label_primary(slots.header_main_label);

        widget.value_label = slots.body_main_label;
        style::apply_label_primary(widget.value_label);
        update_item_value(widget);
        return;
    }

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, widget.def->label);
    style::apply_label_primary(label);

    widget.value_label = lv_label_create(btn);
    style::apply_label_muted(widget.value_label);
    update_item_value(widget);
}

static void build_item_list();

static void platform_delay_ms(uint32_t ms)
{
    device_runtime::delay_ms(ms);
}

static void platform_restart()
{
    device_runtime::restart();
}

static void prefs_put_int_ns(const char* ns, const char* key, int value)
{
    settings_store::put_int(ns, key, value);
}

static void prefs_put_bool_ns(const char* ns, const char* key, bool value)
{
    settings_store::put_bool(ns, key, value);
}

static void prefs_put_uint_ns(const char* ns, const char* key, uint32_t value)
{
    settings_store::put_uint(ns, key, value);
}

static int prefs_get_int_ns(const char* ns, const char* key, int default_value)
{
    return settings_store::get_int(ns, key, default_value);
}

static bool prefs_get_bool_ns(const char* ns, const char* key, bool default_value)
{
    return settings_store::get_bool(ns, key, default_value);
}

static uint32_t prefs_get_uint_ns(const char* ns, const char* key, uint32_t default_value)
{
    return settings_store::get_uint(ns, key, default_value);
}

static void prefs_remove_keys(const char* ns, const char* const* keys, size_t key_count)
{
    settings_store::remove_keys(ns, keys, key_count);
}

static void prefs_clear_ns(const char* ns)
{
    settings_store::clear_namespace(ns);
}

static void prefs_put_int(const char* key, int value)
{
    prefs_put_int_ns(kPrefsNs, key, value);
}

static void prefs_put_bool(const char* key, bool value)
{
    prefs_put_bool_ns(kPrefsNs, key, value);
}

static int prefs_get_int(const char* key, int default_value)
{
    return prefs_get_int_ns(kPrefsNs, key, default_value);
}

static bool prefs_get_bool(const char* key, bool default_value)
{
    return prefs_get_bool_ns(kPrefsNs, key, default_value);
}

static bool is_settings_store_owned_enum_setting(const char* key)
{
    if (!key)
    {
        return false;
    }
    return strcmp(key, "screen_brightness") == 0 ||
           strcmp(key, "speaker_volume") == 0;
}

static bool is_settings_store_owned_toggle_setting(const char* key)
{
    if (!key)
    {
        return false;
    }
    return strcmp(key, "vibration_enabled") == 0 ||
           strcmp(key, "adv_debug") == 0;
}

static uint8_t get_message_tone_volume_default()
{
    return device_runtime::default_message_tone_volume();
}

static void apply_message_tone_volume(uint8_t volume)
{
    device_runtime::set_message_tone_volume(volume);
}

static void play_message_tone_preview()
{
    device_runtime::play_message_tone();
}

static constexpr int kScreenBrightnessMin = DEVICE_MIN_BRIGHTNESS_LEVEL;
static constexpr int kScreenBrightnessMax = DEVICE_MAX_BRIGHTNESS_LEVEL;

static int clamp_screen_brightness(int level)
{
    if (level < kScreenBrightnessMin)
    {
        return kScreenBrightnessMin;
    }
    if (level > kScreenBrightnessMax)
    {
        return kScreenBrightnessMax;
    }
    return level;
}

static void apply_track_recording_runtime(bool enabled)
{
    tracker_runtime::set_auto_recording(enabled);
}

static void apply_track_interval_runtime(uint32_t interval)
{
    if (interval == 99)
    {
        tracker_runtime::set_distance_only(true);
        tracker_runtime::set_interval_seconds(0);
    }
    else
    {
        tracker_runtime::set_distance_only(false);
        tracker_runtime::set_interval_seconds(interval);
    }
}

static void apply_track_format_runtime(uint8_t format)
{
    tracker_runtime::set_format(static_cast<tracker_runtime::Format>(format));
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

static chat::MeshProtocol selected_protocol()
{
    return static_cast<chat::MeshProtocol>(g_settings.chat_protocol);
}

static bool is_meshcore_protocol_selected()
{
    return selected_protocol() == chat::MeshProtocol::MeshCore;
}

static bool is_rnode_protocol_selected()
{
    return selected_protocol() == chat::MeshProtocol::RNode ||
           selected_protocol() == chat::MeshProtocol::LXMF;
}

static void reset_mesh_settings()
{
    app::IAppFacade& app_ctx = app::appFacade();
    app_ctx.getConfig().meshtastic_config = chat::MeshConfig();
    app_ctx.getConfig().meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    app_ctx.getConfig().meshcore_config = chat::MeshConfig();
    app_ctx.getConfig().applyMeshCoreFactoryDefaults();
    app_ctx.getConfig().rnode_config = chat::MeshConfig();
    app_ctx.getConfig().applyRNodeFactoryDefaults();
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
    g_settings.net_tx_power = app_ctx.getConfig().activeMeshConfig().tx_power;
    g_settings.net_hop_limit = app_ctx.getConfig().meshtastic_config.hop_limit;
    const chat::MeshConfig& active_cfg = app_ctx.getConfig().activeMeshConfig();
    g_settings.net_tx_enabled = active_cfg.tx_enabled;
    g_settings.net_relay = app_ctx.getConfig().meshtastic_config.enable_relay;
    g_settings.net_duty_cycle = true;
    g_settings.net_channel_util = 0;
    g_settings.mc_region_preset = app_ctx.getConfig().meshcore_config.meshcore_region_preset;

    static const char* kResetKeys[] = {
        "mesh_protocol",
        "chat_channel",
        "chat_psk",
        "mc_channel_name",
        "mc_channel_key",
        "mc_ch_name",
        "mc_ch_key",
        "net_preset",
        "net_use_preset",
        "net_bw",
        "net_sf",
        "net_cr",
        "net_tx_power",
        "net_hop_limit",
        "net_tx_enabled",
        "net_channel_num",
        "net_freq_offset",
        "net_relay",
        "net_duty_cycle",
        "net_util",
        "mc_freq",
        "mc_bw",
        "mc_sf",
        "mc_cr",
        "mc_tx_power",
        "mc_tx",
        "mc_repeat",
        "mc_rx_delay",
        "mc_airtime",
        "mc_flood_max",
        "mc_multi_acks",
        "mc_channel_slot",
        "mc_ch_slot",
    };
    prefs_remove_keys(kPrefsNs, kResetKeys, sizeof(kResetKeys) / sizeof(kResetKeys[0]));

    build_item_list();
    ::ui::SystemNotification::show(::ui::i18n::tr("Resetting..."), 1500);
    platform_delay_ms(300);
    platform_restart();
}

static void reset_node_db()
{
    app::IAppFacade& app_ctx = app::appFacade();
    app_ctx.clearNodeDb();
    prefs_clear_ns("chat_pki");
    ::ui::SystemNotification::show(::ui::i18n::tr("Node DB reset"), 3000);
}

static void clear_message_db()
{
    app::IAppFacade& app_ctx = app::appFacade();
    app_ctx.clearMessageDb();
    ::ui::SystemNotification::show(::ui::i18n::tr("Message DB cleared"), 3000);
}

static void perform_factory_reset()
{
    static const char* kNamespacesToClear[] = {
        "chat",
        "gps",
        "settings",
        "aprs",
        "power",
        "chat_pki",
    };

    for (const char* ns : kNamespacesToClear)
    {
        prefs_clear_ns(ns);
    }

    team::ui::team_ui_get_store().clear();

    ::ui::SystemNotification::show(::ui::i18n::tr("Resetting..."), 1500);
    platform_delay_ms(300);
    platform_restart();
}

static void settings_load()
{
    app::IAppFacade& app_ctx = app::appFacade();
    g_settings.chat_protocol = static_cast<int>(app_ctx.getConfig().mesh_protocol);

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
    if (kTxPowerOptionCount == 0)
    {
        size_t limit = sizeof(kTxPowerOptions) / sizeof(kTxPowerOptions[0]);
        int value = kNetTxPowerMin;
        while (value <= kNetTxPowerMax && kTxPowerOptionCount < limit)
        {
            snprintf(kTxPowerLabels[kTxPowerOptionCount],
                     sizeof(kTxPowerLabels[kTxPowerOptionCount]),
                     "%d dBm",
                     value);
            kTxPowerOptions[kTxPowerOptionCount].label = kTxPowerLabels[kTxPowerOptionCount];
            kTxPowerOptions[kTxPowerOptionCount].value = value;
            kTxPowerOptionCount++;
            value++;
        }
    }

    kLocaleOptionCount = 0;
    const size_t locale_limit = sizeof(kLocaleOptions) / sizeof(kLocaleOptions[0]);
    for (size_t index = 0; index < ::ui::i18n::locale_count() && kLocaleOptionCount < locale_limit; ++index)
    {
        const ::ui::i18n::LocaleInfo* locale = ::ui::i18n::locale_at(index);
        if (!locale)
        {
            continue;
        }

        const char* display_name =
            (locale->native_name && locale->native_name[0] != '\0') ? locale->native_name : locale->display_name;
        std::snprintf(kLocaleOptionLabels[kLocaleOptionCount],
                      sizeof(kLocaleOptionLabels[kLocaleOptionCount]),
                      "%s",
                      display_name ? display_name : "");
        kLocaleOptions[kLocaleOptionCount].label = kLocaleOptionLabels[kLocaleOptionCount];
        kLocaleOptions[kLocaleOptionCount].value = static_cast<int>(index);
        ++kLocaleOptionCount;
    }

    app_ctx.getEffectiveUserInfo(g_settings.user_name,
                                 sizeof(g_settings.user_name),
                                 g_settings.short_name,
                                 sizeof(g_settings.short_name));
    const app::AppConfig& cfg = app_ctx.getConfig();
    const chat::MeshConfig& mt_cfg = cfg.meshtastic_config;
    const chat::MeshConfig& mc_cfg = cfg.meshcore_config;
    const chat::MeshConfig& rn_cfg = cfg.rnode_config;

    uint32_t gps_interval_seconds = cfg.gps_interval_ms / 1000U;
    if (gps_interval_seconds == 0)
    {
        gps_interval_seconds = 1;
    }
    g_settings.gps_mode = cfg.gps_mode;
    g_settings.gps_sat_mask = cfg.gps_sat_mask;
    g_settings.gps_strategy = cfg.gps_strategy;
    g_settings.gps_interval = static_cast<int>(gps_interval_seconds);
    g_settings.gps_alt_ref = cfg.gps_alt_ref;
    g_settings.gps_coord_format = cfg.gps_coord_format;

    g_settings.map_coord_system = cfg.map_coord_system;
    g_settings.map_source = cfg.map_source;
    if (g_settings.map_source < 0 || g_settings.map_source > 2)
    {
        g_settings.map_source = 0;
    }
    g_settings.map_contour_enabled = cfg.map_contour_enabled;
    g_settings.map_track_enabled = cfg.map_track_enabled;
    g_settings.map_track_interval = cfg.map_track_interval;
    g_settings.map_track_format = cfg.map_track_format;

    g_settings.chat_region = mt_cfg.region;
    g_settings.chat_channel = cfg.chat_channel;
    const uint8_t* active_psk = nullptr;
    if (cfg.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        active_psk = mc_cfg.secondary_key;
    }
    else if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        active_psk = mt_cfg.secondary_key;
    }
    if (!active_psk || is_zero_key(active_psk, sizeof(mt_cfg.secondary_key)))
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

    if (cfg.mesh_protocol == chat::MeshProtocol::RNode ||
        cfg.mesh_protocol == chat::MeshProtocol::LXMF)
    {
        g_settings.net_use_preset = 0;
        g_settings.net_modem_preset = 0;
        g_settings.net_manual_bw = static_cast<int>(std::lround(rn_cfg.bandwidth_khz));
        g_settings.net_manual_sf = rn_cfg.spread_factor;
        g_settings.net_manual_cr = rn_cfg.coding_rate;
        float_to_text(rn_cfg.override_frequency_mhz, g_settings.net_override_freq,
                      sizeof(g_settings.net_override_freq), 3);
    }
    else
    {
        g_settings.net_use_preset = mt_cfg.use_preset;
        g_settings.net_modem_preset = mt_cfg.modem_preset;
        g_settings.net_manual_bw = static_cast<int>(std::lround(mt_cfg.bandwidth_khz));
        g_settings.net_manual_sf = mt_cfg.spread_factor;
        g_settings.net_manual_cr = mt_cfg.coding_rate;
        float_to_text(mt_cfg.override_frequency_mhz, g_settings.net_override_freq,
                      sizeof(g_settings.net_override_freq), 3);
    }

    int tx_power = cfg.activeMeshConfig().tx_power;
    if (tx_power < kNetTxPowerMin) tx_power = kNetTxPowerMin;
    if (tx_power > kNetTxPowerMax) tx_power = kNetTxPowerMax;
    g_settings.net_tx_power = tx_power;
    g_settings.net_hop_limit = mt_cfg.hop_limit;
    g_settings.net_tx_enabled = cfg.activeMeshConfig().tx_enabled;
    g_settings.net_override_duty_cycle = mt_cfg.override_duty_cycle;
    g_settings.net_channel_num = mt_cfg.channel_num;
    g_settings.net_relay = mt_cfg.enable_relay;
    float_to_text(mt_cfg.frequency_offset_mhz, g_settings.net_freq_offset, sizeof(g_settings.net_freq_offset), 3);
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
    int mc_tx_power = mc_cfg.tx_power;
    if (mc_tx_power < kNetTxPowerMin) mc_tx_power = kNetTxPowerMin;
    if (mc_tx_power > kNetTxPowerMax) mc_tx_power = kNetTxPowerMax;
    g_settings.mc_tx_power = mc_tx_power;
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
    g_settings.privacy_nmea_output = cfg.privacy_nmea_output;
    g_settings.privacy_nmea_sentence = cfg.privacy_nmea_sentence;

    g_settings.screen_timeout_ms = static_cast<int>(screen_runtime::timeout_ms());
    g_settings.screen_brightness = clamp_screen_brightness(
        prefs_get_int("screen_brightness", static_cast<int>(device_runtime::screen_brightness())));
    g_settings.timezone_offset_min = ::platform::ui::time::timezone_offset_min();
    g_settings.speaker_volume = prefs_get_int("speaker_volume",
                                              static_cast<int>(get_message_tone_volume_default()));
    if (g_settings.speaker_volume < 0)
    {
        g_settings.speaker_volume = 0;
    }
    if (g_settings.speaker_volume > 100)
    {
        g_settings.speaker_volume = 100;
    }
    apply_message_tone_volume(static_cast<uint8_t>(g_settings.speaker_volume));
    g_settings.display_locale_index = ::ui::i18n::current_locale_index();

    g_settings.ble_enabled = cfg.ble_enabled;
    g_settings.vibration_enabled = prefs_get_bool("vibration_enabled", true);
    refresh_wifi_state_from_runtime();

    g_settings.advanced_debug_logs = prefs_get_bool("adv_debug", false);

    // Gauge capacities (for System > Power settings). Load values from the
    // shared "power" settings namespace into the text fields.
    {
        uint32_t d = prefs_get_uint_ns("power", "gauge_design_mah", 1500);
        uint32_t f = prefs_get_uint_ns("power", "gauge_full_mah", 1500);
        if (d == 0) d = 1500;
        if (f == 0) f = 1500;
        if (d > 10000) d = 10000;
        if (f > 10000) f = 10000;
        snprintf(g_settings.gauge_design_mah, sizeof(g_settings.gauge_design_mah), "%lu",
                 static_cast<unsigned long>(d));
        snprintf(g_settings.gauge_full_mah, sizeof(g_settings.gauge_full_mah), "%lu",
                 static_cast<unsigned long>(f));
    }
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
    case settings::ui::SettingType::Info:
        if (item.text_value && item.text_value[0] != '\0')
        {
            std::snprintf(out, out_len, "%s", item.text_value);
        }
        else
        {
            std::snprintf(out, out_len, "%s", ::ui::i18n::tr("N/A"));
        }
        break;
    case settings::ui::SettingType::Toggle:
        snprintf(out, out_len, "%s", ::ui::i18n::tr((item.bool_value && *item.bool_value) ? "ON" : "OFF"));
        break;
    case settings::ui::SettingType::Enum:
    {
        int value = item.enum_value ? *item.enum_value : 0;
        const char* label = ::ui::i18n::tr("N/A");
        for (size_t i = 0; i < item.option_count; ++i)
        {
            if (item.options[i].value == value)
            {
                label = option_labels_are_translated(item)
                            ? ::ui::i18n::tr(item.options[i].label)
                            : (item.options[i].label ? item.options[i].label : "");
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
            snprintf(out, out_len, "%s", ::ui::i18n::tr("Not set"));
        }
        break;
    case settings::ui::SettingType::Action:
        snprintf(out, out_len, "%s", ::ui::i18n::tr("Run"));
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
    if (option_labels_use_content_font(*widget.def))
    {
        ::ui::i18n::set_content_label_text_raw(widget.value_label, value);
    }
    else
    {
        ::ui::i18n::set_label_text_raw(widget.value_label, value);
    }
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

    const auto modal_size = ::ui::page_profile::resolve_modal_size(width, height, lv_screen_active());
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, modal_size.width, modal_size.height);
    lv_obj_center(win);
    style::apply_modal_panel(win);
    lv_obj_set_style_pad_all(win, ::ui::page_profile::resolve_modal_pad(), LV_PART_MAIN);
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
            app::IAppFacade& app_ctx = app::appFacade();
            strncpy(app_ctx.getConfig().node_name, g_state.editing_item->text_value,
                    sizeof(app_ctx.getConfig().node_name) - 1);
            app_ctx.getConfig().node_name[sizeof(app_ctx.getConfig().node_name) - 1] = '\0';
            app_ctx.saveConfig();
            app_ctx.applyUserInfo();
            broadcast_nodeinfo = true;
        }
        if (is_short_name)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            strncpy(app_ctx.getConfig().short_name, g_state.editing_item->text_value,
                    sizeof(app_ctx.getConfig().short_name) - 1);
            app_ctx.getConfig().short_name[sizeof(app_ctx.getConfig().short_name) - 1] = '\0';
            app_ctx.saveConfig();
            app_ctx.applyUserInfo();
            broadcast_nodeinfo = true;
        }
        if (broadcast_nodeinfo)
        {
            app::appFacade().broadcastNodeInfo();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "chat_psk") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            uint8_t key[16] = {};
            if (!parse_psk(g_state.editing_item->text_value, key, sizeof(key)))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("PSK must be 32 hex or 16 chars"), 4000);
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
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid frequency offset"), 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshtastic_config.frequency_offset_mhz = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "net_override_freq") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid frequency value"), 3000);
                modal_close();
                return;
            }
            if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::RNode ||
                app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::LXMF)
            {
                app_ctx.getConfig().rnode_config.override_frequency_mhz = value;
            }
            else
            {
                app_ctx.getConfig().meshtastic_config.override_frequency_mhz = value;
            }
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_freq") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid MeshCore frequency"), 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_freq_mhz = value;
            app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
            g_settings.mc_region_preset = 0;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_bw") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid MeshCore bandwidth"), 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_bw_khz = value;
            app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
            g_settings.mc_region_preset = 0;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_rx_delay") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid RX delay"), 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_rx_delay_base = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_airtime") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid airtime factor"), 3000);
                modal_close();
                return;
            }
            app_ctx.getConfig().meshcore_config.meshcore_airtime_factor = value;
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_channel_name") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            strncpy(app_ctx.getConfig().meshcore_config.meshcore_channel_name,
                    g_state.editing_item->text_value,
                    sizeof(app_ctx.getConfig().meshcore_config.meshcore_channel_name) - 1);
            app_ctx.getConfig().meshcore_config.meshcore_channel_name[sizeof(app_ctx.getConfig().meshcore_config.meshcore_channel_name) - 1] = '\0';
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "mc_channel_key") == 0)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            uint8_t key[16] = {};
            if (!parse_psk(g_state.editing_item->text_value, key, sizeof(key)))
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Key must be 32 hex or 16 chars"), 3000);
                modal_close();
                return;
            }
            memcpy(app_ctx.getConfig().meshcore_config.secondary_key, key, sizeof(key));
            app_ctx.saveConfig();
            app_ctx.applyMeshConfig();
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "gauge_design_mah") == 0)
        {
            // Update gauge design capacity (mAh) in the shared "power" settings namespace.
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') || value <= 0 || value > 10000)
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid design capacity (mAh)"), 3000);
                modal_close();
                return;
            }
            prefs_put_uint_ns("power", "gauge_design_mah", static_cast<uint32_t>(value));
        }
        if (g_state.editing_item->pref_key && strcmp(g_state.editing_item->pref_key, "gauge_full_mah") == 0)
        {
            // Update gauge full-charge capacity (mAh) that shares the same profile with design capacity.
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') || value <= 0 || value > 10000)
            {
                ::ui::SystemNotification::show(::ui::i18n::tr("Invalid full capacity (mAh)"), 3000);
                modal_close();
                return;
            }
            prefs_put_uint_ns("power", "gauge_full_mah", static_cast<uint32_t>(value));
        }
        if (g_state.editing_item->pref_key &&
            (strcmp(g_state.editing_item->pref_key, "wifi_ssid") == 0 ||
             strcmp(g_state.editing_item->pref_key, "wifi_password") == 0))
        {
            wifi_runtime::Config config{};
            config.enabled = g_settings.wifi_enabled;
            copy_bounded(config.ssid, sizeof(config.ssid), g_settings.wifi_ssid);
            copy_bounded(config.password, sizeof(config.password), g_settings.wifi_password);
            (void)wifi_runtime::save_config(config);
            refresh_wifi_state_from_runtime();
            update_item_value(*g_state.editing_widget);
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
    ::ui::i18n::set_label_text(title, item.label);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    g_state.modal_textarea = lv_textarea_create(win);
    lv_textarea_set_one_line(g_state.modal_textarea, true);
    lv_textarea_set_max_length(g_state.modal_textarea, static_cast<uint16_t>(item.text_max - 1));
    if (item.mask_text)
    {
        lv_textarea_set_password_mode(g_state.modal_textarea, true);
    }
    lv_obj_set_width(g_state.modal_textarea, LV_PCT(100));
    lv_obj_align(g_state.modal_textarea, LV_ALIGN_TOP_MID, 0, ::ui::page_profile::current().large_touch_hitbox ? 40 : 28);
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
    lv_obj_set_size(save_btn, ::ui::page_profile::resolve_control_button_min_width(), ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* save_label = lv_label_create(save_btn);
    ::ui::i18n::set_label_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, on_text_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, ::ui::page_profile::resolve_control_button_min_width(), ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
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
    bool rebuild_active_app = false;
    bool refresh_menu_labels = false;
    int previous_value = *payload->item->enum_value;
    *payload->item->enum_value = payload->value;
    if (is_settings_store_owned_enum_setting(payload->item->pref_key))
    {
        prefs_put_int(payload->item->pref_key, payload->value);
    }
    update_item_value(*payload->widget);
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "display_locale") == 0)
    {
        if (::ui::i18n::set_locale_by_index(static_cast<size_t>(payload->value), true))
        {
            refresh_menu_labels = true;
            rebuild_active_app = true;
        }
        else
        {
            *payload->item->enum_value = previous_value;
        }
        g_settings.display_locale_index = ::ui::i18n::current_locale_index();
        update_item_value(*payload->widget);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "wifi_network") == 0)
    {
        if (payload->value >= 0 &&
            static_cast<size_t>(payload->value) < kWifiNetworkOptionCount)
        {
            const wifi_runtime::ScanResult& result =
                kWifiScanResults[static_cast<size_t>(payload->value)];
            copy_bounded(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), result.ssid);
            wifi_runtime::Config config{};
            config.enabled = g_settings.wifi_enabled;
            copy_bounded(config.ssid, sizeof(config.ssid), g_settings.wifi_ssid);
            copy_bounded(config.password, sizeof(config.password), g_settings.wifi_password);
            (void)wifi_runtime::save_config(config);
            refresh_wifi_state_from_runtime();
            rebuild_list = true;
        }
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mesh_protocol") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        chat::MeshProtocol target = static_cast<chat::MeshProtocol>(payload->value);
        if (!app_ctx.switchMeshProtocol(target, true))
        {
            *payload->item->enum_value = previous_value;
            update_item_value(*payload->widget);
            ::ui::SystemNotification::show(::ui::i18n::tr("Protocol switch failed"), 3000);
        }
        else
        {
            rebuild_list = true;
            ::ui::SystemNotification::show(::ui::i18n::tr("Protocol switched"), 2000);
            restart_now = true;
        }
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "chat_region") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        chat::MeshConfig& mt_cfg = app_ctx.getConfig().meshtastic_config;
        mt_cfg.region = static_cast<uint8_t>(payload->value);
        const auto* region = chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(mt_cfg.region));
        if (region && region->power_limit_dbm > 0)
        {
            int8_t limit = static_cast<int8_t>(region->power_limit_dbm);
            if (limit > kNetTxPowerMax)
            {
                limit = static_cast<int8_t>(kNetTxPowerMax);
            }
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
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshtastic_config.use_preset = (payload->value != 0);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_preset") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshtastic_config.modem_preset = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshtastic_config.use_preset = true;
        g_settings.net_use_preset = true;
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_bw") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::RNode ||
            app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::LXMF)
        {
            app_ctx.getConfig().rnode_config.bandwidth_khz = static_cast<float>(payload->value);
        }
        else
        {
            app_ctx.getConfig().meshtastic_config.bandwidth_khz = static_cast<float>(payload->value);
            app_ctx.getConfig().meshtastic_config.use_preset = false;
            g_settings.net_use_preset = false;
        }
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_sf") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::RNode ||
            app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::LXMF)
        {
            app_ctx.getConfig().rnode_config.spread_factor = static_cast<uint8_t>(payload->value);
        }
        else
        {
            app_ctx.getConfig().meshtastic_config.spread_factor = static_cast<uint8_t>(payload->value);
            app_ctx.getConfig().meshtastic_config.use_preset = false;
            g_settings.net_use_preset = false;
        }
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_cr") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::RNode ||
            app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::LXMF)
        {
            app_ctx.getConfig().rnode_config.coding_rate = static_cast<uint8_t>(payload->value);
        }
        else
        {
            app_ctx.getConfig().meshtastic_config.coding_rate = static_cast<uint8_t>(payload->value);
            app_ctx.getConfig().meshtastic_config.use_preset = false;
            g_settings.net_use_preset = false;
        }
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_hop_limit") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshtastic_config.hop_limit = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_channel_num") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshtastic_config.channel_num = static_cast<uint16_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "screen_timeout") == 0)
    {
        screen_runtime::set_timeout_ms(static_cast<uint32_t>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "screen_brightness") == 0)
    {
        const uint8_t brightness = static_cast<uint8_t>(clamp_screen_brightness(payload->value));
        g_settings.screen_brightness = brightness;
        device_runtime::set_screen_brightness(brightness);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "speaker_volume") == 0)
    {
        const uint8_t volume = static_cast<uint8_t>(payload->value);
        apply_message_tone_volume(volume);
        if (volume > 0)
        {
            // Immediate audible feedback so user can tune the level interactively.
            play_message_tone_preview();
        }
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_interval") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        uint32_t interval_ms = static_cast<uint32_t>(payload->value) * 1000u;
        app_ctx.getConfig().gps_interval_ms = interval_ms;
        app_ctx.saveConfig();
        gps_runtime::set_collection_interval(interval_ms);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_mode") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().gps_mode = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps_runtime::set_gnss_config(app_ctx.getConfig().gps_mode, app_ctx.getConfig().gps_sat_mask);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_sat_mask") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().gps_sat_mask = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps_runtime::set_gnss_config(app_ctx.getConfig().gps_mode, app_ctx.getConfig().gps_sat_mask);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_strategy") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().gps_strategy = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps_runtime::set_power_strategy(static_cast<uint8_t>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_alt_ref") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().gps_alt_ref = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "gps_coord_fmt") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().gps_coord_format = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_coord") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().map_coord_system = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_source") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
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
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().map_track_interval = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        apply_track_interval_runtime(static_cast<uint32_t>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "map_track_format") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().map_track_format = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        apply_track_format_runtime(static_cast<uint8_t>(payload->value));
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "chat_channel") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().chat_channel = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyChatDefaults();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_util") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().net_channel_util = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyNetworkLimits();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_tx_power") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::RNode ||
            app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::LXMF)
        {
            app_ctx.getConfig().rnode_config.tx_power = static_cast<int8_t>(payload->value);
        }
        else
        {
            app_ctx.getConfig().meshtastic_config.tx_power = static_cast<int8_t>(payload->value);
        }
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_region_preset") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        chat::MeshConfig& mc_cfg = app_ctx.getConfig().meshcore_config;
        uint8_t preset_id = static_cast<uint8_t>(payload->value);
        if (!chat::meshcore::isValidRegionPresetId(preset_id))
        {
            preset_id = 0;
        }
        mc_cfg.meshcore_region_preset = preset_id;
        g_settings.mc_region_preset = preset_id;
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
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshcore_config.meshcore_sf = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
        g_settings.mc_region_preset = 0;
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_cr") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshcore_config.meshcore_cr = static_cast<uint8_t>(payload->value);
        app_ctx.getConfig().meshcore_config.meshcore_region_preset = 0;
        g_settings.mc_region_preset = 0;
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_tx_power") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshcore_config.tx_power = static_cast<int8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_flood_max") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshcore_config.meshcore_flood_max = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "mc_channel_slot") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "privacy_encrypt") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().privacy_encrypt_mode = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyPrivacyConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "privacy_nmea") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().privacy_nmea_output = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps_runtime::set_nmea_config(app_ctx.getConfig().privacy_nmea_output,
                                     app_ctx.getConfig().privacy_nmea_sentence);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "privacy_nmea_sent") == 0)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        app_ctx.getConfig().privacy_nmea_sentence = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        gps_runtime::set_nmea_config(app_ctx.getConfig().privacy_nmea_output,
                                     app_ctx.getConfig().privacy_nmea_sentence);
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "timezone_offset") == 0)
    {
        ::platform::ui::time::set_timezone_offset_min(payload->value);
        (void)previous_value;
        restart_now = true;
    }
    modal_close();
    if (refresh_menu_labels)
    {
        ::ui::menu_layout::refresh_localized_text();
    }
    if (rebuild_active_app)
    {
        ui_request_rebuild_active_app();
        return;
    }
    if (rebuild_list)
    {
        build_item_list();
    }
    if (restart_now)
    {
        ::ui::SystemNotification::show(::ui::i18n::tr("Restarting..."), 1500);
        platform_delay_ms(300);
        platform_restart();
    }
}

static void on_option_modal_back_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
}

static void on_factory_reset_confirm_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
    perform_factory_reset();
}

static void on_factory_reset_cancel_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
}

static void open_factory_reset_modal()
{
    if (g_state.modal_root)
    {
        return;
    }

    modal_prepare_group();
    g_state.modal_root = create_modal_root(300, 170);
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text(title, "Factory Reset");
    style::apply_label_primary(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* body = lv_label_create(win);
    lv_obj_set_width(body, LV_PCT(100));
    ::ui::i18n::set_label_text(body, "Clear all settings and restart?");
    style::apply_label_muted(body);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_factory_reset_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* confirm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(confirm_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    ::ui::i18n::set_label_text(confirm_label, "Factory Reset");
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_factory_reset_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_state.modal_group, cancel_btn);
    lv_group_add_obj(g_state.modal_group, confirm_btn);
    lv_group_focus_obj(cancel_btn);
}

static void option_modal_focused_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target && lv_obj_is_valid(target))
    {
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
    }
}

static void open_option_modal(const settings::ui::SettingItem& item, settings::ui::ItemWidget& widget)
{
    if (g_state.modal_root)
    {
        return;
    }
    modal_prepare_group();

    // Overlay only below top bar; list starts 3px under top bar, no title
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t kTopBarH = profile.top_bar_height > 0 ? profile.top_bar_height
                                                           : static_cast<lv_coord_t>(::ui::widgets::kTopBarHeight);
    const lv_coord_t kGapFromTopBar = 3;

    lv_coord_t content_h = lv_obj_get_height(g_state.root) - kTopBarH;
    g_state.modal_root = lv_obj_create(g_state.root);
    lv_obj_set_size(g_state.modal_root, LV_PCT(100), content_h);
    lv_obj_set_pos(g_state.modal_root, 0, kTopBarH);
    style::apply_modal_bg(g_state.modal_root);
    style::apply_modal_panel(g_state.modal_root);
    lv_obj_set_style_border_width(g_state.modal_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.modal_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_state.modal_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(g_state.modal_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_state.modal_root, LV_OBJ_FLAG_CLICKABLE);

    // Single scrollable list: options + Back as last item, 3px from top bar
    lv_obj_t* list = lv_obj_create(g_state.modal_root);
    lv_obj_set_size(list, LV_PCT(100), content_h - kGapFromTopBar);
    lv_obj_set_pos(list, 0, kGapFromTopBar);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    s_option_click_count = 0;
    for (size_t i = 0; i < item.option_count && s_option_click_count < kMaxOptions; ++i)
    {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), ::ui::page_profile::resolve_control_button_height());
        style::apply_btn_modal(btn);
        lv_obj_t* label = lv_label_create(btn);
        if (option_labels_are_translated(item))
        {
            ::ui::i18n::set_label_text(label, item.options[i].label);
        }
        else
        {
            if (option_labels_use_content_font(item))
            {
                ::ui::i18n::set_content_label_text_raw(label, item.options[i].label);
            }
            else
            {
                ::ui::i18n::set_label_text_raw(label, item.options[i].label);
            }
        }
        style::apply_label_primary(label);
        lv_obj_center(label);

        s_option_clicks[s_option_click_count] = {&item, item.options[i].value, &widget};
        lv_obj_add_event_cb(btn, on_option_clicked, LV_EVENT_CLICKED,
                            &s_option_clicks[s_option_click_count]);
        if (item.enum_value && item.options[i].value == *item.enum_value)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(btn, option_modal_focused_cb, LV_EVENT_FOCUSED, nullptr);
        lv_group_add_obj(g_state.modal_group, btn);
        s_option_click_count++;
    }

    // Back as last item inside the list
    lv_obj_t* back_btn = lv_btn_create(list);
    lv_obj_set_size(back_btn, LV_PCT(100), ::ui::page_profile::resolve_control_button_height());
    style::apply_btn_modal(back_btn);
    lv_obj_t* back_label = lv_label_create(back_btn);
    ::ui::i18n::set_label_text(back_label, "Back");
    style::apply_label_primary(back_label);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, on_option_modal_back_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(back_btn, option_modal_focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_group_add_obj(g_state.modal_group, back_btn);

    if (s_option_click_count > 0)
    {
        lv_group_focus_obj(lv_obj_get_child(list, 0));
    }
    else
    {
        lv_group_focus_obj(back_btn);
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
    {"LXMF", static_cast<int>(chat::MeshProtocol::LXMF)},
    {"RNode Bridge", static_cast<int>(chat::MeshProtocol::RNode)},
};

static const settings::ui::SettingOption kNetPresetOptions[] = {
    {"LongFast", meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST},
    {"LongTurbo", meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO},
    {"LongMod", meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE},
    {"LongSlow", meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW},
    {"Invalid", meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW},
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
// Tx power options are populated dynamically based on board limits.
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

static const settings::ui::SettingOption kScreenBrightnessOptions[] = {
    {"OFF", 0},
    {"25%", 4},
    {"50%", 8},
    {"75%", 12},
    {"100%", 16},
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
    {"Protocol", settings::ui::SettingType::Enum, kChatProtocolOptions, 4, &g_settings.chat_protocol, nullptr, nullptr, 0, false, "mesh_protocol"},
    {"Region", settings::ui::SettingType::Enum, kChatRegionOptions, 0, &g_settings.chat_region, nullptr, nullptr, 0, false, "chat_region"},
    {"Channel", settings::ui::SettingType::Enum, kChatChannelOptions, 2, &g_settings.chat_channel, nullptr, nullptr, 0, false, "chat_channel"},
    {"Channel Key / PSK", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.chat_psk, sizeof(g_settings.chat_psk), true, "chat_psk"},
    {"Encryption Mode", settings::ui::SettingType::Enum, kPrivacyEncryptOptions, 3, &g_settings.privacy_encrypt_mode, nullptr, nullptr, 0, false, "privacy_encrypt"},
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
    {"TX Power", settings::ui::SettingType::Enum, kTxPowerOptions,
     0, &g_settings.net_tx_power, nullptr, nullptr, 0, false, "net_tx_power"},
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
    {"MC TX Power", settings::ui::SettingType::Enum, kTxPowerOptions, 0, &g_settings.mc_tx_power, nullptr, nullptr, 0, false, "mc_tx_power"},
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
    {"Display Language", settings::ui::SettingType::Enum, kLocaleOptions,
     0, &g_settings.display_locale_index, nullptr, nullptr, 0, false,
     "display_locale"},
    {"Screen Timeout", settings::ui::SettingType::Enum, kScreenTimeoutOptions, 4, &g_settings.screen_timeout_ms, nullptr, nullptr, 0, false, "screen_timeout"},
    {"Screen Brightness", settings::ui::SettingType::Enum, kScreenBrightnessOptions,
     sizeof(kScreenBrightnessOptions) / sizeof(kScreenBrightnessOptions[0]), &g_settings.screen_brightness, nullptr, nullptr, 0, false, "screen_brightness"},
    {"Speaker Volume", settings::ui::SettingType::Enum, kSpeakerVolumeOptions,
     sizeof(kSpeakerVolumeOptions) / sizeof(kSpeakerVolumeOptions[0]), &g_settings.speaker_volume, nullptr, nullptr, 0, false, "speaker_volume"},
    {"Vibration", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.vibration_enabled, nullptr, 0, false, "vibration_enabled"},
    {"Bluetooth", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.ble_enabled, nullptr, 0, false, "ble_enabled"},
    {"Time Zone", settings::ui::SettingType::Enum, kTimeZoneOptions, sizeof(kTimeZoneOptions) / sizeof(kTimeZoneOptions[0]), &g_settings.timezone_offset_min, nullptr, nullptr, 0, false, "timezone_offset"},
    {"Gauge Design (mAh)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr,
     g_settings.gauge_design_mah, sizeof(g_settings.gauge_design_mah), false, "gauge_design_mah"},
    {"Gauge Full (mAh)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr,
     g_settings.gauge_full_mah, sizeof(g_settings.gauge_full_mah), false, "gauge_full_mah"},
    {"Factory Reset", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "system_factory_reset"},
};

static settings::ui::SettingItem kWifiItems[] = {
    {"Wi-Fi Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.wifi_enabled, nullptr, 0, false, "wifi_enabled"},
    {"Status", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.wifi_status, sizeof(g_settings.wifi_status), false, "wifi_status"},
    {"Scan Networks", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "wifi_scan"},
    {"Detected Network", settings::ui::SettingType::Enum, kWifiNetworkOptions, 0, &g_settings.wifi_network_index, nullptr, nullptr, 0, false, "wifi_network"},
    {"SSID", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), false, "wifi_ssid"},
    {"Password", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.wifi_password, sizeof(g_settings.wifi_password), true, "wifi_password"},
    {"Connect", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "wifi_connect"},
    {"Disconnect", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "wifi_disconnect"},
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
    {"Wi-Fi", kWifiItems, sizeof(kWifiItems) / sizeof(kWifiItems[0])},
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

static int find_filter_index(lv_obj_t* button)
{
    if (!button || !lv_obj_is_valid(button))
    {
        return -1;
    }

    for (size_t i = 0; i < g_state.filter_count; ++i)
    {
        if (g_state.filter_buttons[i] == button)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static settings::ui::ItemWidget* find_item_widget(lv_obj_t* button)
{
    if (!button || !lv_obj_is_valid(button))
    {
        return nullptr;
    }

    for (size_t i = 0; i < g_state.item_count; ++i)
    {
        settings::ui::ItemWidget& widget = g_state.item_widgets[i];
        if (widget.btn == button)
        {
            return &widget;
        }
    }

    return nullptr;
}

static bool select_filter_index(int idx)
{
    if (idx < 0 || static_cast<size_t>(idx) >= g_state.filter_count)
    {
        return false;
    }
    if (s_building_list)
    {
        return false;
    }

    g_state.current_category = idx;
    update_filter_styles();
    build_item_list();
    return true;
}

static bool has_pref_key(const settings::ui::SettingItem& item, const char* key)
{
    return item.pref_key && key && strcmp(item.pref_key, key) == 0;
}

static bool option_labels_are_translated(const settings::ui::SettingItem& item)
{
    return !has_pref_key(item, "display_locale") &&
           !has_pref_key(item, "wifi_network");
}

static bool option_labels_use_content_font(const settings::ui::SettingItem& item)
{
    return has_pref_key(item, "display_locale") ||
           has_pref_key(item, "wifi_network");
}

static bool should_show_item(const settings::ui::SettingItem& item)
{
    if (!item.pref_key)
    {
        return true;
    }

    if (has_pref_key(item, "wifi_network"))
    {
        return wifi_runtime::is_supported() && kWifiNetworkOptionCount > 0;
    }
    if ((has_pref_key(item, "wifi_scan") || has_pref_key(item, "wifi_connect") ||
         has_pref_key(item, "wifi_disconnect") || has_pref_key(item, "wifi_ssid") ||
         has_pref_key(item, "wifi_password") || has_pref_key(item, "wifi_enabled")) &&
        !wifi_runtime::is_supported())
    {
        return false;
    }

    const bool meshcore = is_meshcore_protocol_selected();
    const bool rnode = is_rnode_protocol_selected();

    // Relay is currently not implemented as real forwarding in Meshtastic path.
    if (has_pref_key(item, "net_relay"))
    {
        return false;
    }

    if (has_pref_key(item, "screen_brightness") && !device_runtime::supports_screen_brightness())
    {
        return false;
    }
    if (meshcore)
    {
        if (has_pref_key(item, "chat_region")) return false;
        if (has_pref_key(item, "chat_channel")) return false;
        if (has_pref_key(item, "chat_psk")) return false;

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
    else if (rnode)
    {
        if (has_pref_key(item, "chat_region")) return false;
        if (has_pref_key(item, "chat_channel")) return false;
        if (has_pref_key(item, "chat_psk")) return false;
        if (has_pref_key(item, "privacy_encrypt")) return false;

        if (has_pref_key(item, "net_use_preset")) return false;
        if (has_pref_key(item, "net_preset")) return false;
        if (has_pref_key(item, "net_hop_limit")) return false;
        if (has_pref_key(item, "net_override_duty")) return false;
        if (has_pref_key(item, "net_channel_num")) return false;
        if (has_pref_key(item, "net_freq_offset")) return false;
        if (has_pref_key(item, "net_duty_cycle")) return false;
        if (has_pref_key(item, "net_util")) return false;

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

static void list_item_focused_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED) return;
    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target && lv_obj_is_valid(target))
    {
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
    }
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
        if (widget.def && widget.def->pref_key &&
            strcmp(widget.def->pref_key, "display_locale") == 0)
        {
            const_cast<settings::ui::SettingItem*>(widget.def)->option_count = kLocaleOptionCount;
        }
        if (widget.def && widget.def->pref_key &&
            strcmp(widget.def->pref_key, "wifi_network") == 0)
        {
            const_cast<settings::ui::SettingItem*>(widget.def)->option_count = kWifiNetworkOptionCount;
        }
        if (widget.def && widget.def->pref_key &&
            (strcmp(widget.def->pref_key, "net_tx_power") == 0 || strcmp(widget.def->pref_key, "mc_tx_power") == 0))
        {
            const_cast<settings::ui::SettingItem*>(widget.def)->option_count = kTxPowerOptionCount;
        }
        if (!should_show_item(*widget.def))
        {
            continue;
        }

        lv_obj_t* btn = lv_btn_create(g_state.list_panel);
        configure_list_item_button(btn);
        style::apply_list_item(btn);

        create_item_content(widget, btn);

        widget.btn = btn;
        if (!use_touch_first_settings_mode())
        {
            lv_obj_add_event_cb(btn, on_item_clicked, LV_EVENT_CLICKED, &widget);
        }
        lv_obj_add_event_cb(btn, list_item_focused_cb, LV_EVENT_FOCUSED, nullptr);
        g_state.item_count++;
    }
    if (should_show_settings_list_back_button())
    {
        g_state.list_back_btn = lv_btn_create(g_state.list_panel);
        lv_obj_set_size(g_state.list_back_btn, LV_PCT(100), ::ui::page_profile::resolve_control_button_height());
        lv_obj_set_style_pad_left(g_state.list_back_btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_right(g_state.list_back_btn, 10, LV_PART_MAIN);
        lv_obj_set_flex_flow(g_state.list_back_btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_state.list_back_btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style::apply_list_item(g_state.list_back_btn);
        lv_obj_t* back_label = lv_label_create(g_state.list_back_btn);
        ::ui::i18n::set_label_text(back_label, "Back");
        style::apply_label_primary(back_label);
        lv_obj_add_event_cb(g_state.list_back_btn, on_list_back_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(g_state.list_back_btn, list_item_focused_cb, LV_EVENT_FOCUSED, nullptr);
    }
    settings::ui::input::on_ui_refreshed();
    lv_obj_scroll_to_y(g_state.list_panel, 0, LV_ANIM_OFF);
    lv_obj_invalidate(g_state.list_panel);
    lv_obj_add_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_state.list_panel, LV_SCROLLBAR_MODE_AUTO);
    s_building_list = false;
}

static bool activate_item_widget(settings::ui::ItemWidget& widget)
{
    if (!widget.def)
    {
        return false;
    }

    const SettingItem& item = *widget.def;
    if (item.type == settings::ui::SettingType::Info)
    {
        return false;
    }
    if (item.type == settings::ui::SettingType::Toggle)
    {
        if (item.bool_value)
        {
            *item.bool_value = !(*item.bool_value);
            if (is_settings_store_owned_toggle_setting(item.pref_key))
            {
                prefs_put_bool(item.pref_key, *item.bool_value);
            }
            update_item_value(widget);
            if (item.pref_key && strcmp(item.pref_key, "net_relay") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().meshtastic_config.enable_relay = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "map_track") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().map_track_enabled = *item.bool_value;
                app_ctx.saveConfig();
                apply_track_recording_runtime(*item.bool_value);
            }
            if (item.pref_key && strcmp(item.pref_key, "map_contour") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().map_contour_enabled = *item.bool_value;
                app_ctx.saveConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "net_duty_cycle") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().net_duty_cycle = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyNetworkLimits();
            }
            if (item.pref_key && strcmp(item.pref_key, "net_tx_enabled") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::MeshCore)
                {
                    app_ctx.getConfig().meshcore_config.tx_enabled = *item.bool_value;
                }
                else if (app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::RNode ||
                         app_ctx.getConfig().mesh_protocol == chat::MeshProtocol::LXMF)
                {
                    app_ctx.getConfig().rnode_config.tx_enabled = *item.bool_value;
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
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().meshtastic_config.override_duty_cycle = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
                app_ctx.applyNetworkLimits();
            }
            if (item.pref_key && strcmp(item.pref_key, "mc_repeat") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().meshcore_config.meshcore_client_repeat = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "mc_multi_acks") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().meshcore_config.meshcore_multi_acks = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyMeshConfig();
            }
            if (item.pref_key && strcmp(item.pref_key, "ble_enabled") == 0)
            {
                app::IAppFacade& app_ctx = app::appFacade();
                app_ctx.getConfig().ble_enabled = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.setBleEnabled(*item.bool_value);
            }
            if (item.pref_key && strcmp(item.pref_key, "wifi_enabled") == 0)
            {
                wifi_runtime::Config config{};
                config.enabled = *item.bool_value;
                copy_bounded(config.ssid, sizeof(config.ssid), g_settings.wifi_ssid);
                copy_bounded(config.password, sizeof(config.password), g_settings.wifi_password);
                (void)wifi_runtime::save_config(config);
                if (!wifi_runtime::apply_enabled(config.enabled) && config.enabled)
                {
                    ::ui::SystemNotification::show(::ui::i18n::tr("Wi-Fi start failed"), 3000);
                }
                if (!config.enabled)
                {
                    clear_wifi_scan_options();
                }
                refresh_wifi_state_from_runtime();
                build_item_list();
            }
            if (item.pref_key && strcmp(item.pref_key, "vibration_enabled") == 0 && *item.bool_value)
            {
                device_runtime::trigger_haptic();
            }
        }
        return true;
    }
    if (item.type == settings::ui::SettingType::Enum)
    {
        open_option_modal(item, widget);
        return true;
    }
    if (item.type == settings::ui::SettingType::Text)
    {
        open_text_modal(item, widget);
        return true;
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
        else if (item.pref_key && strcmp(item.pref_key, "system_factory_reset") == 0)
        {
            open_factory_reset_modal();
        }
        else if (item.pref_key && strcmp(item.pref_key, "wifi_scan") == 0)
        {
            std::vector<wifi_runtime::ScanResult> results;
            if (!wifi_runtime::scan(results))
            {
                refresh_wifi_state_from_runtime();
            }
            else
            {
                rebuild_wifi_scan_options(results);
                for (size_t i = 0; i < kWifiNetworkOptionCount; ++i)
                {
                    if (std::strcmp(kWifiScanResults[i].ssid, g_settings.wifi_ssid) == 0)
                    {
                        g_settings.wifi_network_index = static_cast<int>(i);
                        break;
                    }
                }
                refresh_wifi_state_from_runtime();
            }
            build_item_list();
        }
        else if (item.pref_key && strcmp(item.pref_key, "wifi_connect") == 0)
        {
            wifi_runtime::Config config{};
            config.enabled = true;
            copy_bounded(config.ssid, sizeof(config.ssid), g_settings.wifi_ssid);
            copy_bounded(config.password, sizeof(config.password), g_settings.wifi_password);
            g_settings.wifi_enabled = true;
            (void)wifi_runtime::save_config(config);
            if (!wifi_runtime::apply_enabled(true) || !wifi_runtime::connect(&config))
            {
                refresh_wifi_state_from_runtime();
            }
            else
            {
                refresh_wifi_state_from_runtime();
            }
            build_item_list();
        }
        else if (item.pref_key && strcmp(item.pref_key, "wifi_disconnect") == 0)
        {
            wifi_runtime::disconnect();
            refresh_wifi_state_from_runtime();
            build_item_list();
        }
        return true;
    }

    return false;
}

static void on_item_clicked(lv_event_t* e)
{
    settings::ui::ItemWidget* widget = static_cast<settings::ui::ItemWidget*>(lv_event_get_user_data(e));
    if (!widget)
    {
        return;
    }

    (void)activate_item_widget(*widget);
}

static void on_filter_clicked(lv_event_t* e)
{
    intptr_t idx = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (idx < 0)
    {
        return;
    }
    if (select_filter_index(static_cast<int>(idx)))
    {
        settings::ui::input::focus_to_list();
    }
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
    const int idx = s_pending_category;
    s_pending_category = -1;
    lv_obj_clear_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_state.list_panel, LV_SCROLLBAR_MODE_OFF);
    (void)select_filter_index(idx);
}

static void on_list_back_clicked(lv_event_t* /*e*/)
{
    (void)activate_list_back_button(g_state.list_back_btn);
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
        lv_obj_set_size(btn, LV_PCT(100), ::ui::page_profile::current().filter_button_height);
        style::apply_btn_filter(btn);
        if (!use_touch_first_settings_mode())
        {
            lv_obj_add_event_cb(btn, on_filter_clicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
            lv_obj_add_event_cb(btn, on_filter_focused, LV_EVENT_FOCUSED, reinterpret_cast<void*>(i));
        }
        lv_obj_t* label = lv_label_create(btn);
        ::ui::i18n::set_label_text(label, kCategories[i].label);
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

bool activate_filter_button(lv_obj_t* filter_button)
{
    return select_filter_index(find_filter_index(filter_button));
}

bool activate_list_button(lv_obj_t* list_button)
{
    settings::ui::ItemWidget* widget = find_item_widget(list_button);
    if (!widget)
    {
        return false;
    }

    return activate_item_widget(*widget);
}

bool activate_list_back_button(lv_obj_t* list_back_button)
{
    if (!should_show_settings_list_back_button() ||
        list_back_button == nullptr ||
        list_back_button != g_state.list_back_btn)
    {
        return false;
    }

    settings::ui::input::focus_to_filter();
    return true;
}

} // namespace settings::ui::components
