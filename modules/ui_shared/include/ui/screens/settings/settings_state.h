/**
 * @file settings_state.h
 * @brief Settings UI state and data
 */

#pragma once

#include "lvgl.h"
#include "platform/ui/auto_reply_settings.h"
#include "ui/widgets/top_bar.h"
#include <cstddef>
#include <cstdint>

namespace settings::ui
{

enum class SettingType
{
    Info,
    Toggle,
    Enum,
    Text,
    Action,
};

enum class SettingId : std::uint16_t
{
    Unknown,
    ChatUser,
    ChatShort,
    MeshProtocol,
    ChatMessageAlerts,
    ChatContactAlerts,
    ChatAutoReplyEnabled,
    ChatAutoReplyText,
    ChatRegion,
    ChatChannel,
    ChatPsk,
    MtPrimaryEnabled,
    MtPrimaryName,
    MtPrimaryKey,
    MtPrimaryKeyGenerate,
    MtPrimaryUplink,
    MtPrimaryDownlink,
    MtSecondaryEnabled,
    MtSecondaryName,
    MtSecondaryKey,
    MtSecondaryKeyGenerate,
    MtSecondaryUplink,
    MtSecondaryDownlink,
    PrivacyEncrypt,
    MtMqttEnabled,
    MtMqttHost,
    MtMqttPort,
    MtMqttRoot,
    MtMqttUser,
    MtMqttPass,
    MtMqttUplink,
    MtMqttDownlink,
    McMqttEnabled,
    McMqttHost,
    McMqttPort,
    McMqttRoot,
    McMqttUser,
    McMqttPass,
    McMqttUplink,
    McMqttDownlink,
    McChannelSlot,
    McChannelEnabled,
    McChannelName,
    McChannelKey,
    McChannelKeyGenerate,
    McChannelClear,
    RtBearer,
    RtLoraEnabled,
    RtDisplayName,
    RtIdentityHash,
    RtLxmfAddress,
    RtWifiGateway,
    RtTcpSlot,
    RtTcpDefaults,
    RtWifiHost,
    RtWifiPort,
    RtWifiAuto,
    RtAnonymousPeer,
    RtLocationRequests,
    NetUsePreset,
    NetPreset,
    NetBw,
    NetSf,
    NetCr,
    NetTxPower,
    NetHopLimit,
    NetTxEnabled,
    NetOverrideDuty,
    NetChannelNum,
    NetFreqOffset,
    NetOverrideFreq,
    NetRelay,
    NetDutyCycle,
    NetUtil,
    McRegionPreset,
    McFreq,
    McBw,
    McSf,
    McCr,
    McTxPower,
    McRepeat,
    McRxDelay,
    McAirtime,
    McFloodMax,
    McMultiAcks,
    McSendProfile,
    McForwardProfile,
    GpsEnabled,
    GpsInitBaud,
    GpsInitProbeMs,
    GpsInitProfile,
    GpsInitRxm,
    GpsInitGnss,
    GpsInitNmea,
    GpsMode,
    GpsSatMask,
    GpsStrategy,
    GpsInterval,
    GpsAltRef,
    GpsCoordFmt,
    ExternalNmea,
    ExternalNmeaSent,
    GpsDiagnostics,
    MapCoord,
    MapSource,
    MapContour,
    MapTrack,
    MapTrackInterval,
    MapTrackFormat,
    DisplayLocale,
    EnabledImes,
    ScreenTimeout,
    ScreenBrightness,
    SpeakerVolume,
    VibrationEnabled,
    C6CompanionStatus,
    C6EnterDownload,
    TimezoneProfile,
    ManualTimeSet,
    GaugeDesignMah,
    GaugeFullMah,
    SpiDiagnostics,
    WifiEnabled,
    WifiStatus,
    WifiScan,
    WifiNetwork,
    WifiSsid,
    WifiPassword,
    WifiConnect,
    WifiDisconnect,
    FwCurrent,
    FwLatest,
    FwStatus,
    FwCheck,
    FwInstall,
    AdvDebug,
    ChatResetMesh,
    ChatResetNodes,
    ChatClearMessages,
    SystemFactoryReset,
};

struct SettingOption
{
    const char* label;
    int value;
};

struct SettingItem
{
    const char* label;
    SettingType type;
    const SettingOption* options;
    size_t option_count;
    int* enum_value;
    bool* bool_value;
    char* text_value;
    size_t text_max;
    bool mask_text;
    const char* pref_key;
    SettingId id = SettingId::Unknown;
};

struct SettingsData
{
    // GPS
    bool gps_enabled = true;
    int gps_init_baud = 0;
    int gps_init_probe_ms = 900;
    int gps_init_profile = 0;
    int gps_init_rxm_policy = 0;
    int gps_init_gnss_policy = 0;
    int gps_init_nmea_policy = 0;
    int gps_mode = 0;
    int gps_sat_mask = 0x1 | 0x8 | 0x4;
    int gps_strategy = 0;
    int gps_interval = 1;
    int gps_alt_ref = 0;
    int gps_coord_format = 0;

    // Map
    int map_coord_system = 0;
    int map_source = 0;
    bool map_contour_enabled = false;
    bool map_track_enabled = false;
    int map_track_interval = 1;
    int map_track_format = 0;

    // Chat
    char user_name[32] = "";
    char short_name[16] = "";
    int chat_protocol = 1;
    int chat_region = 0;
    int chat_channel = 0;
    char chat_psk[65] = {};
    int chat_message_alerts = 1;
    int chat_contact_alerts = 1;
    bool chat_auto_reply_enabled = false;
    char chat_auto_reply_text[::platform::ui::auto_reply::kTextMaxBytes + 1] = {};

    // Meshtastic channels
    bool mt_primary_enabled = true;
    char mt_primary_name[32] = "LongFast";
    char mt_primary_key[65] = {};
    bool mt_primary_uplink = false;
    bool mt_primary_downlink = false;
    bool mt_secondary_enabled = false;
    char mt_secondary_name[32] = "Secondary";
    char mt_secondary_key[65] = {};
    bool mt_secondary_uplink = false;
    bool mt_secondary_downlink = false;

    // Network
    int net_use_preset = 1;
    int net_modem_preset = 0;
    int net_manual_bw = 250;
    int net_manual_sf = 11;
    int net_manual_cr = 5;
    int net_tx_power = 14;
    int net_hop_limit = 2;
    bool net_tx_enabled = true;
    bool net_override_duty_cycle = false;
    int net_channel_num = 0;
    char net_freq_offset[16] = "0";
    char net_override_freq[16] = "0";
    bool net_relay = true;
    bool net_duty_cycle = true;
    int net_channel_util = 0;
    int rt_bearer_policy = 0;
    bool rt_lora_enabled = true;
    bool rt_wifi_gateway_enabled = true;
    bool rt_wifi_auto_connect = true;
    bool rt_anonymous_peer = false;
    bool rt_location_requests = false;
    char rt_display_name[32] = "--";
    char rt_identity_hash[36] = "--";
    char rt_lxmf_address[36] = "--";
    char rt_wifi_gateway_host[64] = "";
    int rt_tcp_slot = 0;
    char rt_wifi_gateway_port[6] = "4242";

    // Meshtastic MQTT
    bool mt_mqtt_enabled = false;
    bool mt_mqtt_uplink = true;
    bool mt_mqtt_downlink = true;
    char mt_mqtt_host[64] = "mqtt.meshtastic.org";
    char mt_mqtt_port[6] = "1883";
    char mt_mqtt_root[64] = "msh/CN";
    char mt_mqtt_user[64] = "meshdev";
    char mt_mqtt_pass[64] = "large4cats";

    // MeshCore
    int mc_region_preset = 0;
    char mc_freq[16] = "915.0";
    char mc_bw[16] = "125.0";
    int mc_sf = 9;
    int mc_cr = 5;
    int mc_tx_power = 14;
    bool mc_client_repeat = false;
    char mc_rx_delay[16] = "0";
    char mc_airtime[16] = "1";
    int mc_flood_max = 16;
    bool mc_multi_acks = false;
    int mc_send_profile = 1;
    int mc_forward_profile = 1;
    int mc_channel_slot = 0;
    bool mc_channel_enabled = true;
    char mc_channel_name[32] = "Public";
    char mc_channel_key[65] = {};
    bool mc_mqtt_enabled = false;
    bool mc_mqtt_uplink = true;
    bool mc_mqtt_downlink = true;
    char mc_mqtt_host[64] = "";
    char mc_mqtt_port[6] = "1883";
    char mc_mqtt_root[64] = "meshcore";
    char mc_mqtt_user[64] = "";
    char mc_mqtt_pass[64] = "";

    // Chat/privacy controls
    int privacy_encrypt_mode = 1;
    int external_nmea_output_hz = 0;
    int external_nmea_sentence_mask = 0;

    // Screen
    int screen_timeout_ms = 30000;
    int screen_brightness = 16;
    int timezone_offset_min = 0;
    int timezone_profile_id = 0;
    char manual_time_year[5] = "";
    char manual_time_month[3] = "";
    char manual_time_day[3] = "";
    char manual_time_hour[3] = "";
    char manual_time_minute[3] = "";
    char manual_time_second[3] = "";
    int speaker_volume = 45;
    int display_locale_index = 0;
    char c6_companion_status[96] = "";
    bool vibration_enabled = true;

    // Wi-Fi
    bool wifi_enabled = false;
    int wifi_network_index = -1;
    char wifi_status[96] = "";
    char wifi_ssid[33] = "";
    char wifi_password[65] = "";

    // Power / Gauge (System)
    char gauge_design_mah[8] = "";
    char gauge_full_mah[8] = "";

    // Advanced
    char fw_current_version[24] = "";
    char fw_latest_version[24] = "";
    char fw_update_status[96] = "";
    bool advanced_debug_logs = false;
};

struct ItemWidget
{
    const SettingItem* def = nullptr;
    lv_obj_t* btn = nullptr;
    lv_obj_t* value_label = nullptr;
};

struct UiState
{
    lv_obj_t* parent = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* filter_panel = nullptr;
    lv_obj_t* list_panel = nullptr;
    // The scrolling panel remains stable. Rebuilt list content is constructed
    // in a hidden sibling and swapped only after it is complete, so settings
    // updates never clear the user-visible container.
    lv_obj_t* list_content = nullptr;
    lv_obj_t* visible_list_content = nullptr;
    lv_obj_t* list_back_btn = nullptr;
    ::ui::widgets::TopBar top_bar;
    lv_obj_t* filter_buttons[8]{};
    size_t filter_count = 0;
    ItemWidget item_widgets[64]{};
    size_t item_count = 0;
    int current_category = 0;

    // Modals
    lv_obj_t* modal_root = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_obj_t* modal_textarea = nullptr;
    lv_obj_t* modal_error = nullptr;
    const SettingItem* editing_item = nullptr;
    ItemWidget* editing_widget = nullptr;
};

extern SettingsData g_settings;
extern UiState g_state;

} // namespace settings::ui
