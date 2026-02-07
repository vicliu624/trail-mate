/**
 * @file settings_page_components.cpp
 * @brief Settings UI components implementation
 */

#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>

#include "../../../app/app_context.h"
#include "../../../chat/domain/chat_types.h"
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

constexpr size_t kMaxItems = 12;
constexpr size_t kMaxOptions = 40;
constexpr const char* kPrefsNs = "settings";
constexpr int kNetTxPowerMin = -9;
constexpr int kNetTxPowerMax = 22;

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

static void prefs_put_str(const char* key, const char* value)
{
    Preferences prefs;
    prefs.begin(kPrefsNs, false);
    prefs.putString(key, value ? value : "");
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

static void mark_restart_required()
{
    g_settings.needs_restart = true;
    prefs_put_bool("needs_restart", true);
    ::ui::SystemNotification::show("Restart required", 4000);
}

static void reset_mesh_settings()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    app_ctx.getConfig().mesh_config = chat::MeshConfig();
    app_ctx.getConfig().mesh_config.region = app::AppConfig::kDefaultRegionCode;
    app_ctx.getConfig().mesh_protocol = chat::MeshProtocol::Meshtastic;
    app_ctx.saveConfig();
    app_ctx.applyMeshConfig();

    g_settings.chat_protocol = static_cast<int>(app_ctx.getConfig().mesh_protocol);
    g_settings.chat_region = app_ctx.getConfig().mesh_config.region;
    g_settings.chat_channel = 0;
    g_settings.chat_psk[0] = '\0';
    g_settings.net_modem_preset = app_ctx.getConfig().mesh_config.modem_preset;
    g_settings.net_tx_power = app_ctx.getConfig().mesh_config.tx_power;
    g_settings.net_relay = app_ctx.getConfig().mesh_config.enable_relay;
    g_settings.net_duty_cycle = true;
    g_settings.net_channel_util = 0;
    g_settings.needs_restart = false;

    Preferences prefs;
    prefs.begin(kPrefsNs, false);
    prefs.remove("mesh_protocol");
    prefs.remove("chat_region");
    prefs.remove("chat_channel");
    prefs.remove("chat_psk");
    prefs.remove("net_preset");
    prefs.remove("net_tx_power");
    prefs.remove("net_relay");
    prefs.remove("net_duty_cycle");
    prefs.remove("net_util");
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

    g_settings.gps_mode = prefs_get_int("gps_mode", 0);
    g_settings.gps_sat_mask = prefs_get_int("gps_sat_mask", 0x1 | 0x8 | 0x4);
    g_settings.gps_strategy = prefs_get_int("gps_strategy", 0);
    g_settings.gps_interval = prefs_get_int("gps_interval", 1);
    g_settings.gps_alt_ref = prefs_get_int("gps_alt_ref", 0);
    g_settings.gps_coord_format = prefs_get_int("gps_coord_fmt", 0);

    g_settings.map_coord_system = prefs_get_int("map_coord", 0);
    g_settings.map_source = prefs_get_int("map_source", 0);
    g_settings.map_track_enabled = prefs_get_bool("map_track", false);
    g_settings.map_track_interval = prefs_get_int("map_track_interval", 1);
    g_settings.map_track_format = prefs_get_int("map_track_format", 0);

    app_ctx.getEffectiveUserInfo(g_settings.user_name,
                                 sizeof(g_settings.user_name),
                                 g_settings.short_name,
                                 sizeof(g_settings.short_name));
    g_settings.chat_region = app_ctx.getConfig().mesh_config.region;
    g_settings.chat_channel = prefs_get_int("chat_channel", 0);
    if (is_zero_key(app_ctx.getConfig().mesh_config.secondary_key,
                    sizeof(app_ctx.getConfig().mesh_config.secondary_key)))
    {
        g_settings.chat_psk[0] = '\0';
    }
    else
    {
        bytes_to_hex(app_ctx.getConfig().mesh_config.secondary_key,
                     sizeof(app_ctx.getConfig().mesh_config.secondary_key),
                     g_settings.chat_psk,
                     sizeof(g_settings.chat_psk));
    }

    g_settings.net_modem_preset = app_ctx.getConfig().mesh_config.modem_preset;
    int tx_power = app_ctx.getConfig().mesh_config.tx_power;
    if (tx_power < kNetTxPowerMin) tx_power = kNetTxPowerMin;
    if (tx_power > kNetTxPowerMax) tx_power = kNetTxPowerMax;
    g_settings.net_tx_power = tx_power;
    g_settings.net_relay = app_ctx.getConfig().mesh_config.enable_relay;
    g_settings.net_duty_cycle = prefs_get_bool("net_duty_cycle", true);
    g_settings.net_channel_util = prefs_get_int("net_util", 0);

    g_settings.privacy_encrypt_mode = prefs_get_int("privacy_encrypt", 1);
    g_settings.privacy_pki = prefs_get_bool("privacy_pki", false);
    g_settings.privacy_nmea_output = prefs_get_int("privacy_nmea", 0);
    g_settings.privacy_nmea_sentence = prefs_get_int("privacy_nmea_sent", 0);

    g_settings.screen_timeout_ms = prefs_get_int("screen_timeout", static_cast<int>(getScreenSleepTimeout()));
    g_settings.timezone_offset_min = prefs_get_int("timezone_offset", 0);

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
        if (!is_user_name && !is_short_name)
        {
            prefs_put_str(g_state.editing_item->pref_key, g_state.editing_item->text_value);
        }
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
            memcpy(app_ctx.getConfig().mesh_config.secondary_key, key, sizeof(key));
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
        app_ctx.getConfig().mesh_config.region = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        restart_now = true;
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "net_preset") == 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        app_ctx.getConfig().mesh_config.modem_preset = static_cast<uint8_t>(payload->value);
        app_ctx.saveConfig();
        app_ctx.applyMeshConfig();
    }
    if (payload->item->pref_key && strcmp(payload->item->pref_key, "screen_timeout") == 0)
    {
        setScreenSleepTimeout(static_cast<uint32_t>(payload->value));
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
        app_ctx.getConfig().map_source = static_cast<uint8_t>(payload->value);
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
        app_ctx.getConfig().mesh_config.tx_power = static_cast<int8_t>(payload->value);
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
    {"Offline Tiles", 0},
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
    {"MediumFast", meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST},
    {"MediumSlow", meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW},
    {"ShortFast", meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST},
    {"ShortSlow", meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW},
    {"ShortTurbo", meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO},
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
};

static settings::ui::SettingItem kMapItems[] = {
    {"Coordinate System", settings::ui::SettingType::Enum, kMapCoordOptions, 3, &g_settings.map_coord_system, nullptr, nullptr, 0, false, "map_coord"},
    {"Map Source", settings::ui::SettingType::Enum, kMapSourceOptions, 1, &g_settings.map_source, nullptr, nullptr, 0, false, "map_source"},
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
    {"Reset Mesh Params", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_reset_mesh"},
    {"Reset Node DB", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_reset_nodes"},
    {"Clear Message DB", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_clear_messages"},
};

static settings::ui::SettingItem kNetworkItems[] = {
    {"Modem Preset", settings::ui::SettingType::Enum, kNetPresetOptions, 8, &g_settings.net_modem_preset, nullptr, nullptr, 0, false, "net_preset"},
    {"TX Power", settings::ui::SettingType::Enum, kNetTxPowerOptions,
     sizeof(kNetTxPowerOptions) / sizeof(kNetTxPowerOptions[0]), &g_settings.net_tx_power, nullptr, nullptr, 0, false, "net_tx_power"},
    {"Relay / Repeater", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_relay, nullptr, 0, false, "net_relay"},
    {"Duty Cycle Limit", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_duty_cycle, nullptr, 0, false, "net_duty_cycle"},
    {"Channel Utilization", settings::ui::SettingType::Enum, kNetUtilOptions, 3, &g_settings.net_channel_util, nullptr, nullptr, 0, false, "net_util"},
};

static settings::ui::SettingItem kPrivacyItems[] = {
    {"Encryption Mode", settings::ui::SettingType::Enum, kPrivacyEncryptOptions, 3, &g_settings.privacy_encrypt_mode, nullptr, nullptr, 0, false, "privacy_encrypt"},
    {"PKI", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.privacy_pki, nullptr, 0, false, "privacy_pki"},
    {"NMEA Output", settings::ui::SettingType::Enum, kPrivacyNmeaOptions, 3, &g_settings.privacy_nmea_output, nullptr, nullptr, 0, false, "privacy_nmea"},
    {"NMEA Sentences", settings::ui::SettingType::Enum, kPrivacyNmeaSentenceOptions, 3, &g_settings.privacy_nmea_sentence, nullptr, nullptr, 0, false, "privacy_nmea_sent"},
};

static settings::ui::SettingItem kScreenItems[] = {
    {"Screen Timeout", settings::ui::SettingType::Enum, kScreenTimeoutOptions, 4, &g_settings.screen_timeout_ms, nullptr, nullptr, 0, false, "screen_timeout"},
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
    {"Privacy", kPrivacyItems, sizeof(kPrivacyItems) / sizeof(kPrivacyItems[0])},
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

static bool should_show_item(const settings::ui::SettingItem& item)
{
    if (!item.pref_key)
    {
        return true;
    }
    if (g_settings.chat_protocol == static_cast<int>(chat::MeshProtocol::MeshCore))
    {
        if (strcmp(item.pref_key, "chat_region") == 0) return false;
        if (strcmp(item.pref_key, "chat_channel") == 0) return false;
        if (strcmp(item.pref_key, "chat_psk") == 0) return false;
        if (strcmp(item.pref_key, "net_preset") == 0) return false;
        if (strcmp(item.pref_key, "net_tx_power") == 0) return false;
        if (strcmp(item.pref_key, "net_relay") == 0) return false;
        if (strcmp(item.pref_key, "net_duty_cycle") == 0) return false;
        if (strcmp(item.pref_key, "net_util") == 0) return false;
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
        if (widget.def == &kChatItems[3])
        {
            kChatItems[3].option_count = kChatRegionOptionCount;
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
                app_ctx.getConfig().mesh_config.enable_relay = *item.bool_value;
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
            if (item.pref_key && strcmp(item.pref_key, "net_duty_cycle") == 0)
            {
                app::AppContext& app_ctx = app::AppContext::getInstance();
                app_ctx.getConfig().net_duty_cycle = *item.bool_value;
                app_ctx.saveConfig();
                app_ctx.applyNetworkLimits();
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
