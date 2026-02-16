/**
 * @file settings_state.h
 * @brief Settings UI state and data
 */

#pragma once

#include "../../widgets/top_bar.h"
#include "lvgl.h"
#include <cstddef>

namespace settings::ui
{

enum class SettingType
{
    Toggle,
    Enum,
    Text,
    Action,
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
};

struct SettingsData
{
    // GPS
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
    char chat_psk[33] = {};
    bool needs_restart = false;

    // Network
    int net_modem_preset = 0;
    int net_tx_power = 14;
    bool net_relay = true;
    bool net_duty_cycle = true;
    int net_channel_util = 0;

    // Chat/GPS (privacy-related controls)
    int privacy_encrypt_mode = 1;
    bool privacy_pki = false;
    int privacy_nmea_output = 0;
    int privacy_nmea_sentence = 0;

    // Screen
    int screen_timeout_ms = 30000;
    int timezone_offset_min = 0;

    // Advanced
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
    lv_obj_t* list_back_btn = nullptr;
    ::ui::widgets::TopBar top_bar;
    lv_obj_t* filter_buttons[8]{};
    size_t filter_count = 0;
    ItemWidget item_widgets[12]{};
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
