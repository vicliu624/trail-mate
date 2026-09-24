/**
 * @file settings_page_components.cpp
 * @brief Settings UI components implementation
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "board/BoardBase.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/ports/i_mesh_adapter.h"
#include "meshtastic/config.pb.h"
#include "platform/ui/auto_reply_settings.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/firmware_update_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/screen_brightness_steps.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_reset_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/spi_diagnostics_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/timezone_profile.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "platform/ui/wireless_companion_runtime.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/page/page_profile.h"
#include "ui/presentation_sources/runtime_settings_source.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/settings/settings_channel_actions.h"
#include "ui/screens/settings/settings_item_layout.h"
#include "ui/screens/settings/settings_page_components.h"
#include "ui/screens/settings/settings_page_input.h"
#include "ui/screens/settings/settings_page_layout.h"
#include "ui/screens/settings/settings_page_styles.h"
#include "ui/screens/settings/settings_spec.h"
#include "ui/screens/settings/settings_state.h"
#include "ui/ui_common.h"
#include "ui/widgets/foreground_operation_overlay.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui/widgets/text_candidate_picker.h"
#include "ui/widgets/top_bar.h"
#include "ui_presentation/settings/settings_model.h"

#if UI_SHARED_TOUCH_IME_ENABLED
#include "ui/LV_Helper.h"
#endif

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif

namespace settings::ui::components
{

namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kLogTag = "settings-page";
#endif

namespace device_runtime = ::platform::ui::device;
namespace firmware_update_runtime = ::platform::ui::firmware_update;
namespace gps_runtime = ::platform::ui::gps;
namespace screen_runtime = ::platform::ui::screen;
namespace settings_store = ::platform::ui::settings_store;
#if defined(ARDUINO_ARCH_ESP32)
namespace spi_diagnostics_runtime = ::platform::ui::spi_diagnostics;
#endif
namespace tracker_runtime = ::platform::ui::tracker;
namespace wireless_companion_runtime = ::platform::ui::wireless_companion;
namespace wifi_runtime = ::platform::ui::wifi;

constexpr size_t kMaxItems = 48;
constexpr size_t kMaxOptions = 40;
constexpr size_t kMaxWifiNetworks = 8;
constexpr size_t kChatRegionOptionCapacity = 32;
constexpr size_t kMeshCoreRegionPresetOptionCapacity = 32;
constexpr size_t kLocaleOptionCapacity = 16;
constexpr size_t kTimeZoneOptionCapacity = 32;
constexpr const char* kPrefsNs = "settings";
constexpr int kChatContactAlertsNone = 0;
constexpr int kChatContactAlertsContacts = 1;
constexpr int kChatContactAlertsAll = 2;
constexpr int kNetTxPowerMin = app::AppConfig::kTxPowerMinDbm;
constexpr int kNetTxPowerMax = app::AppConfig::kTxPowerMaxDbm;
constexpr size_t kTxPowerOptionCapacity =
    static_cast<size_t>(kNetTxPowerMax - kNetTxPowerMin + 1);
constexpr int kGpsInitProbeMinMs = 250;
constexpr int kGpsInitProbeMaxMs = 1600;
#if defined(ARDUINO_T_DECK_PRO)
constexpr uint32_t kSettingsAmber = 0x000000;
constexpr uint32_t kSettingsAmberDark = 0x000000;
constexpr uint32_t kSettingsText = 0x000000;
#else
constexpr uint32_t kSettingsAmber = 0xEBA341;
constexpr uint32_t kSettingsAmberDark = 0xC98118;
constexpr uint32_t kSettingsText = 0x6B4A1E;
#endif

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

struct ImeToggleClick
{
    const char* ime_id = nullptr;
    settings::ui::ItemWidget* widget = nullptr;
    lv_obj_t* state_label = nullptr;
};

struct DynamicOptionStorage
{
    settings::ui::SettingOption chat_region_options[kChatRegionOptionCapacity] = {};
    settings::ui::SettingOption meshcore_region_preset_options[kMeshCoreRegionPresetOptionCapacity] = {};
    settings::ui::SettingOption tx_power_options[kTxPowerOptionCapacity] = {};
    char tx_power_labels[kTxPowerOptionCapacity][12] = {};
    settings::ui::SettingOption locale_options[kLocaleOptionCapacity] = {};
    char locale_option_labels[kLocaleOptionCapacity][48] = {};
    settings::ui::SettingOption time_zone_options[kTimeZoneOptionCapacity] = {};
    char custom_time_zone_label[32] = {};
    settings::ui::SettingOption wifi_network_options[kMaxWifiNetworks] = {};
    char wifi_network_option_labels[kMaxWifiNetworks][64] = {};
    wifi_runtime::ScanResult wifi_scan_results[kMaxWifiNetworks] = {};
};

DynamicOptionStorage& dynamic_options()
{
    static DynamicOptionStorage* storage = []() -> DynamicOptionStorage*
    {
#if defined(ESP_PLATFORM)
        void* ptr = heap_caps_malloc_prefer(sizeof(DynamicOptionStorage),
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (ptr == nullptr)
        {
            ptr = ::operator new(sizeof(DynamicOptionStorage));
        }
        return new (ptr) DynamicOptionStorage();
#else
        return new DynamicOptionStorage();
#endif
    }();
    return *storage;
}

gps_runtime::GpsReceiverInitConfig make_gps_receiver_init_config(const app::AppConfig& config)
{
    gps_runtime::GpsReceiverInitConfig init{};
    init.baud = config.gps_init_baud;
    init.probe_ms = config.gps_init_probe_ms;
    init.profile = config.gps_init_profile;
    init.rxm_policy = config.gps_init_rxm_policy;
    init.gnss_policy = config.gps_init_gnss_policy;
    init.nmea_policy = config.gps_init_nmea_policy;
    return init;
}

static OptionClick s_option_clicks[kMaxOptions]{};
static constexpr size_t kMaxImeOptions = 16;
static ImeToggleClick s_ime_toggle_clicks[kMaxImeOptions]{};
static size_t s_option_click_count = 0;
static size_t s_ime_toggle_count = 0;
static lv_group_t* s_modal_prev_group = nullptr;
static int s_pending_category = -1;
static bool s_category_update_scheduled = false;
static bool s_building_list = false;
static size_t kChatRegionOptionCount = 0;
static size_t kMeshCoreRegionPresetOptionCount = 0;
static size_t kTxPowerOptionCount = 0;
static size_t kLocaleOptionCount = 0;
static size_t kTimeZoneOptionCount = 0;
static size_t kWifiNetworkOptionCount = 0;
static lv_timer_t* s_firmware_update_timer = nullptr;
static firmware_update_runtime::Phase s_last_firmware_phase = firmware_update_runtime::Phase::Unsupported;
static bool s_last_firmware_busy = false;
static std::uint32_t s_settings_busy_generation = 0;
static lv_obj_t* s_gps_diagnostics_label = nullptr;
static lv_obj_t* s_spi_diagnostics_label = nullptr;
static char s_spi_diagnostics_text[384]{};
static char s_spi_diagnostics_init_rate[24]{};

static std::uint32_t next_settings_busy_generation()
{
    ++s_settings_busy_generation;
    if (s_settings_busy_generation == 0)
    {
        ++s_settings_busy_generation;
    }
    return s_settings_busy_generation;
}

class ScopedSettingsBusyOverlay
{
  public:
    explicit ScopedSettingsBusyOverlay(const char* title,
                                       const char* detail = nullptr,
                                       int progress_percent = -1)
        : generation_(next_settings_busy_generation())
    {
        namespace foreground = ::ui::widgets::foreground_operation;
        foreground::publish(
            foreground::make_snapshot(foreground::Slot::SettingsAction,
                                      foreground::Policy::Overlay,
                                      foreground::Priority::Blocking,
                                      title,
                                      detail,
                                      progress_percent,
                                      nullptr,
                                      generation_));
        active_ = true;
    }

    ~ScopedSettingsBusyOverlay()
    {
        if (!active_)
        {
            return;
        }
        namespace foreground = ::ui::widgets::foreground_operation;
        foreground::clear(foreground::Slot::SettingsAction, generation_);
    }

    ScopedSettingsBusyOverlay(const ScopedSettingsBusyOverlay&) = delete;
    ScopedSettingsBusyOverlay& operator=(const ScopedSettingsBusyOverlay&) = delete;

    void update(const char* title, const char* detail = nullptr, int progress_percent = -1)
    {
        if (!active_)
        {
            return;
        }
        namespace foreground = ::ui::widgets::foreground_operation;
        foreground::publish(
            foreground::make_snapshot(foreground::Slot::SettingsAction,
                                      foreground::Policy::Overlay,
                                      foreground::Priority::Blocking,
                                      title,
                                      detail,
                                      progress_percent,
                                      nullptr,
                                      generation_));
    }

  private:
    std::uint32_t generation_ = 0;
    bool active_ = false;
};

enum ManualDateTimeField : size_t
{
    kManualDateTimeYear = 0,
    kManualDateTimeMonth,
    kManualDateTimeDay,
    kManualDateTimeHour,
    kManualDateTimeMinute,
    kManualDateTimeSecond,
    kManualDateTimeFieldCount,
};

constexpr int kManualDateTimeYearMin = 2020;
constexpr int kManualDateTimeYearMax = 2099;
constexpr size_t kManualDateTimeFocusCapacity = kManualDateTimeFieldCount + 2;
static lv_obj_t* s_manual_time_rollers[kManualDateTimeFieldCount] = {};
static char s_manual_year_options[400] = {};
static char s_manual_month_options[36] = {};
static char s_manual_day_options[93] = {};
static char s_manual_hour_options[72] = {};
static char s_manual_minute_options[180] = {};
static char s_manual_second_options[180] = {};
static lv_obj_t* s_manual_datetime_focus_order[kManualDateTimeFocusCapacity] = {};
static size_t s_manual_datetime_focus_count = 0;
static std::unique_ptr<::ui::widgets::ImeWidget> s_text_modal_ime;

static ::ui::settings::SettingsModel& settings_model()
{
    static ::ui::settings::SettingsModel model(
        ::ui::presentation_sources::runtime_settings_source(),
        ::ui::presentation_sources::runtime_settings_action_sink());
    return model;
}

static bool apply_settings_bool_patch(const char* key, bool value)
{
    ::ui::settings::SettingsPatchView patch;
    ::ui::copyText(patch.key, key);
    ::ui::copyText(patch.value, value ? "1" : "0");
    return settings_model().apply(patch).ok;
}

static void refresh_timezone_options()
{
    auto& options = dynamic_options();
    size_t profile_count = 0;
    const auto* profiles = ::platform::ui::time::timezone_profiles(&profile_count);
    kTimeZoneOptionCount = 0;
    const size_t limit = kTimeZoneOptionCapacity;
    for (size_t i = 0; profiles && i < profile_count && kTimeZoneOptionCount < limit; ++i)
    {
        options.time_zone_options[kTimeZoneOptionCount++] = {profiles[i].label, profiles[i].id};
    }
    options.custom_time_zone_label[0] = '\0';
}

static void append_custom_timezone_option_if_needed(int profile_id, int offset_min)
{
    if (!::platform::ui::time::timezone_profile_id_is_fixed(profile_id))
    {
        return;
    }
    auto& options = dynamic_options();
    if (kTimeZoneOptionCount >= kTimeZoneOptionCapacity)
    {
        return;
    }

    char tzdef[24] = {};
    ::platform::ui::time::build_fixed_posix_tzdef(offset_min, tzdef, sizeof(tzdef));
    std::snprintf(options.custom_time_zone_label,
                  sizeof(options.custom_time_zone_label),
                  "Fixed %s",
                  tzdef[0] != '\0' ? tzdef : "UTC");
    options.time_zone_options[kTimeZoneOptionCount++] = {options.custom_time_zone_label, profile_id};
}

static void update_item_value(settings::ui::ItemWidget& widget);
static settings::ui::SettingId item_id(const settings::ui::SettingItem& item);
static void open_factory_reset_modal();
static void open_gps_diagnostics_modal();
static void open_spi_diagnostics_modal();
static void open_enabled_imes_modal(settings::ui::ItemWidget& widget);
static void open_manual_datetime_modal(settings::ui::ItemWidget& widget);
static bool option_labels_are_translated(const settings::ui::SettingItem& item);
static bool option_labels_use_content_font(const settings::ui::SettingItem& item);
static void apply_locale_preview_font(lv_obj_t* label, const settings::ui::SettingItem& item, int value);
static void refresh_language_pack_options();
static void bind_dynamic_option_storage_to_items();
static void refresh_reticulum_identity_fields_from_runtime();

static void copy_bounded(char* out, size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

static void append_bounded(char* out, size_t out_len, const char* text)
{
    if (!out || out_len == 0 || !text)
    {
        return;
    }

    size_t used = 0;
    while (used < out_len && out[used] != '\0')
    {
        ++used;
    }
    if (used >= out_len - 1)
    {
        return;
    }

    const size_t remaining = out_len - used - 1;
    size_t count = 0;
    while (count < remaining && text[count] != '\0')
    {
        out[used + count] = text[count];
        ++count;
    }
    out[used + count] = '\0';
}

static void set_modal_toggle_state_label(lv_obj_t* label, bool enabled)
{
    if (!label)
    {
        return;
    }
    ::ui::i18n::set_label_text_raw(label, ::ui::i18n::tr(enabled ? "ON" : "OFF"));
}

static void format_enabled_ime_summary(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const std::size_t available = ::ui::i18n::ime_count();
    const std::size_t enabled = ::ui::i18n::enabled_ime_count();
    if (available == 0)
    {
        std::snprintf(out, out_len, "%s", ::ui::i18n::tr("Unavailable"));
        return;
    }
    if (enabled == 0)
    {
        std::snprintf(out, out_len, "%s", ::ui::i18n::tr("None"));
        return;
    }
    if (enabled == 1)
    {
        for (std::size_t index = 0; index < available; ++index)
        {
            const ::ui::i18n::ImeInfo* ime = ::ui::i18n::ime_at(index);
            if (ime != nullptr && ::ui::i18n::ime_enabled(ime->id))
            {
                std::snprintf(out, out_len, "%s", ime->display_name ? ime->display_name : ime->id);
                return;
            }
        }
    }

    const std::string summary = ::ui::i18n::format("%d enabled", static_cast<int>(enabled));
    std::snprintf(out, out_len, "%s", summary.c_str());
}

static void clear_wifi_scan_options()
{
    auto& options = dynamic_options();
    kWifiNetworkOptionCount = 0;
    for (size_t i = 0; i < kMaxWifiNetworks; ++i)
    {
        options.wifi_network_options[i] = settings::ui::SettingOption{};
        options.wifi_network_option_labels[i][0] = '\0';
        options.wifi_scan_results[i] = wifi_runtime::ScanResult{};
    }
    g_settings.wifi_network_index = -1;
}

static void rebuild_wifi_scan_options(size_t result_count)
{
    auto& options = dynamic_options();

    const size_t limit = result_count < kMaxWifiNetworks ? result_count : kMaxWifiNetworks;
    for (size_t i = 0; i < limit; ++i)
    {
        std::snprintf(options.wifi_network_option_labels[i],
                      sizeof(options.wifi_network_option_labels[i]),
                      "%s (%d dBm%s)",
                      options.wifi_scan_results[i].ssid,
                      options.wifi_scan_results[i].rssi,
                      options.wifi_scan_results[i].requires_password ? ", lock" : "");
        options.wifi_network_options[i].label = options.wifi_network_option_labels[i];
        options.wifi_network_options[i].value = static_cast<int>(i);
    }
    kWifiNetworkOptionCount = limit;
}

static void refresh_wifi_status_from_runtime()
{
    const wifi_runtime::Status status = wifi_runtime::status();
    if (status.connected && status.ip[0] != '\0')
    {
        copy_bounded(g_settings.wifi_status, sizeof(g_settings.wifi_status), status.ip);
        return;
    }

    if (!status.supported && status.message[0] == '\0')
    {
        copy_bounded(g_settings.wifi_status,
                     sizeof(g_settings.wifi_status),
                     "Wi-Fi unsupported");
    }
    else
    {
        copy_bounded(g_settings.wifi_status,
                     sizeof(g_settings.wifi_status),
                     status.message);
    }
}

static void refresh_wifi_state_from_runtime()
{
    wifi_runtime::Config config{};
    (void)wifi_runtime::load_config(config);

    g_settings.wifi_enabled = config.enabled;
    copy_bounded(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), config.ssid);
    copy_bounded(g_settings.wifi_password, sizeof(g_settings.wifi_password), config.password);
    refresh_wifi_status_from_runtime();
}

static void firmware_status_summary(const firmware_update_runtime::Status& status,
                                    char* out,
                                    size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const char* message = status.message[0] != '\0'
                              ? status.message
                              : (status.supported ? "Ready to check" : "OTA unsupported");
    if (status.phase == firmware_update_runtime::Phase::Error &&
        status.detail[0] != '\0' &&
        std::strcmp(status.detail, message) != 0)
    {
        copy_bounded(out, out_len, message);
        append_bounded(out, out_len, ": ");
        append_bounded(out, out_len, status.detail);
        return;
    }

    copy_bounded(out, out_len, message);
}

static void refresh_firmware_update_state_from_runtime()
{
    const firmware_update_runtime::Status status = firmware_update_runtime::status();
    copy_bounded(g_settings.fw_current_version,
                 sizeof(g_settings.fw_current_version),
                 status.current_version[0] != '\0' ? status.current_version : "unknown");
    if (status.latest_version[0] != '\0')
    {
        copy_bounded(g_settings.fw_latest_version, sizeof(g_settings.fw_latest_version), status.latest_version);
    }
    else
    {
        copy_bounded(g_settings.fw_latest_version,
                     sizeof(g_settings.fw_latest_version),
                     status.checked ? g_settings.fw_current_version : "Not checked");
    }
    firmware_status_summary(status, g_settings.fw_update_status, sizeof(g_settings.fw_update_status));
}

static void refresh_wireless_companion_state_from_runtime()
{
    const wireless_companion_runtime::Status status = wireless_companion_runtime::status();
    copy_bounded(g_settings.c6_companion_status,
                 sizeof(g_settings.c6_companion_status),
                 status.message);
    if (status.detail[0] != '\0')
    {
        append_bounded(g_settings.c6_companion_status, sizeof(g_settings.c6_companion_status), " (");
        append_bounded(g_settings.c6_companion_status, sizeof(g_settings.c6_companion_status), status.detail);
        append_bounded(g_settings.c6_companion_status, sizeof(g_settings.c6_companion_status), ")");
    }
}

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static constexpr int kDays[] = {
        31,
        28,
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    };
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && is_leap_year(year))
    {
        return 29;
    }
    return kDays[month - 1];
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned month_prime = month > 2 ? month - 3U : month + 9U;
    const unsigned doy = (153U * month_prime + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

static time_t utc_epoch_from_parts(int year,
                                   int month,
                                   int day,
                                   int hour,
                                   int minute,
                                   int second)
{
    const int64_t days = days_from_civil(year,
                                         static_cast<unsigned>(month),
                                         static_cast<unsigned>(day));
    const int64_t seconds =
        days * 86400LL +
        static_cast<int64_t>(hour) * 3600LL +
        static_cast<int64_t>(minute) * 60LL +
        static_cast<int64_t>(second);
    return static_cast<time_t>(seconds);
}

static bool parse_int_range(const char* text, int min_value, int max_value, int& out_value)
{
    if (text == nullptr || text[0] == '\0')
    {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || (end && *end != '\0') || value < min_value || value > max_value)
    {
        return false;
    }
    out_value = static_cast<int>(value);
    return true;
}

static int clamp_int(int value, int min_value, int max_value)
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

static void format_fixed_2(char* dest, size_t dest_size, int value)
{
    if (dest == nullptr || dest_size < 3)
    {
        return;
    }
    const int bounded = clamp_int(value, 0, 99);
    dest[0] = static_cast<char>('0' + (bounded / 10));
    dest[1] = static_cast<char>('0' + (bounded % 10));
    dest[2] = '\0';
}

static void format_fixed_4(char* dest, size_t dest_size, int value)
{
    if (dest == nullptr || dest_size < 5)
    {
        return;
    }
    const int bounded = clamp_int(value, 0, 9999);
    dest[0] = static_cast<char>('0' + ((bounded / 1000) % 10));
    dest[1] = static_cast<char>('0' + ((bounded / 100) % 10));
    dest[2] = static_cast<char>('0' + ((bounded / 10) % 10));
    dest[3] = static_cast<char>('0' + (bounded % 10));
    dest[4] = '\0';
}

static bool parse_manual_time_fields(time_t& out_utc)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parse_int_range(g_settings.manual_time_year, 2020, 2099, year) ||
        !parse_int_range(g_settings.manual_time_month, 1, 12, month) ||
        !parse_int_range(g_settings.manual_time_day, 1, 31, day) ||
        !parse_int_range(g_settings.manual_time_hour, 0, 23, hour) ||
        !parse_int_range(g_settings.manual_time_minute, 0, 59, minute) ||
        !parse_int_range(g_settings.manual_time_second, 0, 59, second))
    {
        return false;
    }
    if (day > days_in_month(year, month))
    {
        return false;
    }

    const time_t local_epoch = utc_epoch_from_parts(year, month, day, hour, minute, second);
    time_t utc_epoch = local_epoch - static_cast<time_t>(g_settings.timezone_offset_min) * 60;
    for (int i = 0; i < 2; ++i)
    {
        const int offset = ::platform::ui::time::timezone_offset_for_profile_id_at(
            g_settings.timezone_profile_id,
            g_settings.timezone_offset_min,
            utc_epoch);
        utc_epoch = local_epoch - static_cast<time_t>(offset) * 60;
    }
    out_utc = utc_epoch;
    return out_utc >= 1577836800;
}

static void refresh_manual_time_fields_from_runtime()
{
    tm local{};
    if (!::platform::ui::time::localtime_now(&local))
    {
        g_settings.manual_time_year[0] = '\0';
        g_settings.manual_time_month[0] = '\0';
        g_settings.manual_time_day[0] = '\0';
        g_settings.manual_time_hour[0] = '\0';
        g_settings.manual_time_minute[0] = '\0';
        g_settings.manual_time_second[0] = '\0';
        return;
    }

    const int year = clamp_int(local.tm_year + 1900, 2020, 2099);
    const int month = clamp_int(local.tm_mon + 1, 1, 12);
    const int day = clamp_int(local.tm_mday, 1, days_in_month(year, month));
    format_fixed_4(g_settings.manual_time_year, sizeof(g_settings.manual_time_year), year);
    format_fixed_2(g_settings.manual_time_month, sizeof(g_settings.manual_time_month), month);
    format_fixed_2(g_settings.manual_time_day, sizeof(g_settings.manual_time_day), day);
    format_fixed_2(g_settings.manual_time_hour,
                   sizeof(g_settings.manual_time_hour),
                   clamp_int(local.tm_hour, 0, 23));
    format_fixed_2(g_settings.manual_time_minute,
                   sizeof(g_settings.manual_time_minute),
                   clamp_int(local.tm_min, 0, 59));
    format_fixed_2(g_settings.manual_time_second,
                   sizeof(g_settings.manual_time_second),
                   clamp_int(local.tm_sec, 0, 59));
}

static bool apply_manual_datetime_setting()
{
    time_t utc_epoch = 0;
    if (!parse_manual_time_fields(utc_epoch))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Invalid date/time"), 3000);
        return false;
    }
    if (!::platform::ui::time::set_utc_time(utc_epoch))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Set time unsupported"), 3000);
        return false;
    }
    refresh_manual_time_fields_from_runtime();
    ::ui::feedback::show_notice(::ui::i18n::tr("Date/time updated"), 2000);
    return true;
}

static void format_manual_datetime_summary(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (g_settings.manual_time_year[0] == '\0' ||
        g_settings.manual_time_month[0] == '\0' ||
        g_settings.manual_time_day[0] == '\0' ||
        g_settings.manual_time_hour[0] == '\0' ||
        g_settings.manual_time_minute[0] == '\0')
    {
        std::snprintf(out, out_len, "%s", ::ui::i18n::tr("Not set"));
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%s-%s-%s %s:%s",
                  g_settings.manual_time_year,
                  g_settings.manual_time_month,
                  g_settings.manual_time_day,
                  g_settings.manual_time_hour,
                  g_settings.manual_time_minute);
}

static void refresh_visible_item_values()
{
    refresh_reticulum_identity_fields_from_runtime();
    for (size_t index = 0; index < g_state.item_count; ++index)
    {
        update_item_value(g_state.item_widgets[index]);
    }
}

static const char* firmware_overlay_title(const firmware_update_runtime::Status& status)
{
    switch (status.phase)
    {
    case firmware_update_runtime::Phase::Checking:
        return "Checking for updates...";
    case firmware_update_runtime::Phase::Downloading:
        return "Downloading update...";
    case firmware_update_runtime::Phase::Installing:
        return "Installing update...";
    case firmware_update_runtime::Phase::Rebooting:
        return "Restarting...";
    default:
        break;
    }
    return status.message[0] != '\0' ? status.message : "Working...";
}

static void sync_firmware_update_ui(bool notify_completion)
{
    const firmware_update_runtime::Status status = firmware_update_runtime::status();
    refresh_firmware_update_state_from_runtime();
    refresh_visible_item_values();

    if (status.busy)
    {
        namespace foreground = ::ui::widgets::foreground_operation;
        const char* detail = status.detail[0] != '\0' ? status.detail : nullptr;
        foreground::publish(
            foreground::make_snapshot(foreground::Slot::FirmwareUpdate,
                                      foreground::Policy::Overlay,
                                      foreground::Priority::Critical,
                                      firmware_overlay_title(status),
                                      detail,
                                      status.progress_percent));
    }
    else
    {
        ::ui::widgets::foreground_operation::clear(
            ::ui::widgets::foreground_operation::Slot::FirmwareUpdate);
    }

    if (notify_completion && s_last_firmware_busy && !status.busy)
    {
        switch (status.phase)
        {
        case firmware_update_runtime::Phase::UpToDate:
            ::ui::feedback::show_notice(status.message, 2200);
            break;
        case firmware_update_runtime::Phase::UpdateAvailable:
            ::ui::feedback::show_notice(status.message, 2600);
            break;
        case firmware_update_runtime::Phase::Error:
        {
            char summary[160];
            firmware_status_summary(status, summary, sizeof(summary));
            ::ui::feedback::show_notice(summary, 3800);
            break;
        }
        default:
            break;
        }
    }

    s_last_firmware_busy = status.busy;
    s_last_firmware_phase = status.phase;
}

static void firmware_update_timer_cb(lv_timer_t* /*timer*/)
{
    refresh_wifi_status_from_runtime();
    sync_firmware_update_ui(true);
}

static constexpr bool use_touch_first_settings_mode()
{
#if defined(ARDUINO_T_DECK)
    return true;
#else
    return false;
#endif
}

static bool should_show_settings_list_back_button()
{
    return !use_touch_first_settings_mode();
}

static void configure_list_item_button(lv_obj_t* btn)
{
    lv_obj_set_width(btn, LV_PCT(100));
    const bool dense = ::ui::page_profile::is_dense();
    lv_obj_set_style_pad_left(btn, dense ? 6 : 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, dense ? 6 : 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn, dense ? 4 : 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void create_item_content(settings::ui::ItemWidget& widget, lv_obj_t* btn)
{
    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, widget.def->label);
    style::apply_label_primary(label);

    widget.value_label = lv_label_create(btn);
    style::apply_label_muted(widget.value_label);
    update_item_value(widget);
    const auto id = item_id(*widget.def);
    const bool action_only = widget.def->type == settings::ui::SettingType::Action &&
                             id != settings::ui::SettingId::EnabledImes &&
                             id != settings::ui::SettingId::ManualTimeSet;
    const bool multiline_value = id == settings::ui::SettingId::RtIdentityHash ||
                                 id == settings::ui::SettingId::RtLxmfAddress;
    settings::ui::item_layout::apply(btn, label, widget.value_label,
                                     ::ui::page_profile::resolve_control_button_height(), !action_only, multiline_value);
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

static constexpr int kScreenBrightnessMax = DEVICE_MAX_BRIGHTNESS_LEVEL;

static int clamp_screen_brightness(int level)
{
    return platform::ui::screen_brightness_steps::clampLevel(level, kScreenBrightnessMax);
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

static void bytes_to_wrapped_hash(const uint8_t* data, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!data || out_len < 35)
    {
        copy_bounded(out, out_len, "--");
        return;
    }

    static const char* kHex = "0123456789ABCDEF";
    size_t cursor = 0;
    for (size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        if (index == 8)
        {
            out[cursor++] = '\n';
        }
        const uint8_t b = data[index];
        out[cursor++] = kHex[b >> 4];
        out[cursor++] = kHex[b & 0x0F];
    }
    out[cursor] = '\0';
}

static void refresh_reticulum_identity_fields(app::IAppFacade& app_ctx,
                                              const app::AppConfig& cfg)
{
    copy_bounded(g_settings.rt_identity_hash,
                 sizeof(g_settings.rt_identity_hash),
                 "--");
    copy_bounded(g_settings.rt_lxmf_address,
                 sizeof(g_settings.rt_lxmf_address),
                 "--");
    copy_bounded(g_settings.rt_display_name,
                 sizeof(g_settings.rt_display_name),
                 "--");
    if (!chat::infra::isReticulumMeshProtocol(cfg.mesh_protocol))
    {
        return;
    }

    const chat::IMeshAdapter* adapter = app_ctx.getMeshAdapter();
    chat::ReticulumLocalIdentityInfo info{};
    if (!adapter || !adapter->getReticulumLocalIdentityInfo(&info) || !info.ready)
    {
        return;
    }

    bytes_to_wrapped_hash(info.identity_hash,
                          g_settings.rt_identity_hash,
                          sizeof(g_settings.rt_identity_hash));
    bytes_to_wrapped_hash(info.lxmf_address,
                          g_settings.rt_lxmf_address,
                          sizeof(g_settings.rt_lxmf_address));
    copy_bounded(g_settings.rt_display_name,
                 sizeof(g_settings.rt_display_name),
                 info.display_name[0] != '\0' ? info.display_name : "--");
}

static void refresh_reticulum_identity_fields_from_runtime()
{
    if (!app::hasAppFacade())
    {
        return;
    }
    app::IAppFacade& app_ctx = app::appFacade();
    refresh_reticulum_identity_fields(app_ctx, app_ctx.readConfig());
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
    return chat::infra::normalizeMeshProtocol(
        static_cast<chat::MeshProtocol>(g_settings.chat_protocol));
}

static bool is_meshcore_protocol_selected()
{
    return selected_protocol() == chat::MeshProtocol::MeshCore;
}

static bool is_meshtastic_protocol_selected()
{
    return selected_protocol() == chat::MeshProtocol::Meshtastic;
}

static bool is_reticulum_protocol_selected()
{
    return chat::infra::isReticulumMeshProtocol(selected_protocol());
}

static chat::ReticulumInterfacePolicy reticulum_bearer_policy_from_value(int value)
{
    switch (value)
    {
    case static_cast<int>(chat::ReticulumInterfacePolicy::LoRaOnly):
        return chat::ReticulumInterfacePolicy::LoRaOnly;
    case static_cast<int>(chat::ReticulumInterfacePolicy::WifiGatewayOnly):
        return chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    case static_cast<int>(chat::ReticulumInterfacePolicy::All):
    default:
        return chat::ReticulumInterfacePolicy::All;
    }
}

static int reticulum_bearer_policy_to_value(chat::ReticulumInterfacePolicy policy)
{
    return static_cast<int>(reticulum_bearer_policy_from_value(static_cast<int>(policy)));
}

static void apply_reticulum_bearer_policy(chat::MeshConfig& config,
                                          chat::ReticulumInterfacePolicy policy)
{
    config.reticulum_interface_policy = reticulum_bearer_policy_from_value(
        static_cast<int>(policy));
    switch (config.reticulum_interface_policy)
    {
    case chat::ReticulumInterfacePolicy::LoRaOnly:
        config.reticulum_lora_enabled = true;
        config.reticulum_wifi_gateway_enabled = false;
        break;
    case chat::ReticulumInterfacePolicy::WifiGatewayOnly:
        config.reticulum_lora_enabled = false;
        config.reticulum_wifi_gateway_enabled = true;
        break;
    case chat::ReticulumInterfacePolicy::All:
    default:
        config.reticulum_interface_policy = chat::ReticulumInterfacePolicy::All;
        config.reticulum_lora_enabled = true;
        config.reticulum_wifi_gateway_enabled = true;
        break;
    }
}

template <typename Mutator>
static bool commit_app_config(app::IAppFacade& app_ctx,
                              app::AppConfigChangeSet changes,
                              Mutator mutator)
{
    auto edit = app_ctx.beginConfigEdit();
    if (!edit)
    {
        return false;
    }

    mutator(edit.config());
    edit.commit(changes);
    return true;
}

static bool reticulum_wifi_settings_visible()
{
    return reticulum_bearer_policy_from_value(g_settings.rt_bearer_policy) !=
           chat::ReticulumInterfacePolicy::LoRaOnly;
}

static bool reticulum_lora_settings_visible()
{
    return reticulum_bearer_policy_from_value(g_settings.rt_bearer_policy) !=
           chat::ReticulumInterfacePolicy::WifiGatewayOnly;
}

static void reset_mesh_settings()
{
    app::IAppFacade& app_ctx = app::appFacade();
    if (!commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [](app::AppConfig& config)
            {
                config.meshtastic_config = chat::MeshConfig();
                config.meshtastic_config.region =
                    app::AppConfig::kDefaultRegionCode;
                config.applyMeshtasticMqttFactoryDefaults();
                config.meshcore_config = chat::MeshConfig();
                config.applyMeshCoreFactoryDefaults();
                config.reticulumConfig() = chat::MeshConfig();
                config.applyReticulumFactoryDefaults();
                config.meshcore_config.resetMeshCoreChannels();
            }))
    {
        return;
    }
    app_ctx.applyMeshConfig();

    g_settings.chat_protocol = static_cast<int>(app_ctx.readConfig().mesh_protocol);
    g_settings.chat_region = app_ctx.readConfig().meshtastic_config.region;
    g_settings.chat_channel = 0;
    g_settings.chat_psk[0] = '\0';
    ::settings::ui::channel::sync_meshtastic_channel_fields(app_ctx.readConfig(), g_settings);
    g_settings.net_use_preset = app_ctx.readConfig().meshtastic_config.use_preset;
    g_settings.net_modem_preset = app_ctx.readConfig().meshtastic_config.modem_preset;
    g_settings.net_tx_power = app_ctx.readConfig().activeMeshConfig().tx_power;
    g_settings.net_hop_limit = app_ctx.readConfig().meshtastic_config.hop_limit;
    const chat::MeshConfig& active_cfg = app_ctx.readConfig().activeMeshConfig();
    g_settings.net_tx_enabled = active_cfg.tx_enabled;
    g_settings.net_relay = app_ctx.readConfig().meshtastic_config.enable_relay;
    g_settings.net_duty_cycle = true;
    g_settings.net_channel_util = 0;
    g_settings.rt_bearer_policy =
        reticulum_bearer_policy_to_value(app_ctx.readConfig().reticulumConfig().reticulum_interface_policy);
    g_settings.rt_lora_enabled = app_ctx.readConfig().reticulumConfig().reticulum_lora_enabled;
    g_settings.rt_wifi_gateway_enabled =
        app_ctx.readConfig().reticulumConfig().reticulum_wifi_gateway_enabled;
    g_settings.rt_wifi_auto_connect =
        app_ctx.readConfig().reticulumConfig().reticulum_wifi_auto_connect;
    g_settings.rt_anonymous_peer =
        app_ctx.readConfig().reticulumConfig().reticulum_anonymous_peer;
    g_settings.rt_location_requests =
        app_ctx.readConfig().reticulumConfig().reticulum_allow_location_requests;
    g_settings.mt_mqtt_enabled = app_ctx.readConfig().meshtastic_mqtt_enabled;
    g_settings.mt_mqtt_uplink = app_ctx.readConfig().meshtastic_mqtt_uplink_enabled;
    g_settings.mt_mqtt_downlink = app_ctx.readConfig().meshtastic_mqtt_downlink_enabled;
    copy_bounded(g_settings.mt_mqtt_host,
                 sizeof(g_settings.mt_mqtt_host),
                 app_ctx.readConfig().meshtastic_mqtt_host);
    std::snprintf(g_settings.mt_mqtt_port,
                  sizeof(g_settings.mt_mqtt_port),
                  "%u",
                  static_cast<unsigned>(app_ctx.readConfig().meshtastic_mqtt_port));
    copy_bounded(g_settings.mt_mqtt_root,
                 sizeof(g_settings.mt_mqtt_root),
                 app_ctx.readConfig().meshtastic_mqtt_root);
    copy_bounded(g_settings.mt_mqtt_user,
                 sizeof(g_settings.mt_mqtt_user),
                 app_ctx.readConfig().meshtastic_mqtt_username);
    copy_bounded(g_settings.mt_mqtt_pass,
                 sizeof(g_settings.mt_mqtt_pass),
                 app_ctx.readConfig().meshtastic_mqtt_password);
    g_settings.mc_region_preset = app_ctx.readConfig().meshcore_config.meshcore_region_preset;

    static const char* kResetKeys[] = {
        "mesh_protocol",
        "chat_channel",
        "chat_psk",
        "mt_primary_enabled",
        "mt_primary_name",
        "mt_primary_key",
        "mt_primary_uplink",
        "mt_primary_downlink",
        "mt_secondary_enabled",
        "mt_secondary_name",
        "mt_secondary_key",
        "mt_secondary_uplink",
        "mt_secondary_downlink",
        "mt_mqtt_enabled",
        "mt_mqtt_uplink",
        "mt_mqtt_downlink",
        "mt_mqtt_host",
        "mt_mqtt_port",
        "mt_mqtt_root",
        "mt_mqtt_user",
        "mt_mqtt_pass",
        "mc_channel_name",
        "mc_channel_key",
        "mc_channel_clear",
        "mc_mqtt_enabled",
        "mc_mqtt_uplink",
        "mc_mqtt_downlink",
        "mc_mqtt_host",
        "mc_mqtt_port",
        "mc_mqtt_root",
        "mc_mqtt_user",
        "mc_mqtt_pass",
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
        "mc_send_prof",
        "mc_fwd_prof",
        "mc_channel_slot",
        "mc_channel_enabled",
        "mc_ch_slot",
        "rt_bearer",
        "rt_lora_enabled",
        "rt_wifi_gateway",
        "rt_wifi_host",
        "rt_wifi_port",
        "rt_wifi_auto",
        "rt_anonymous_peer",
    };
    prefs_remove_keys(kPrefsNs, kResetKeys, sizeof(kResetKeys) / sizeof(kResetKeys[0]));

    build_item_list();
    ::ui::feedback::show_notice(::ui::i18n::tr("Resetting..."), 1500);
    platform_delay_ms(300);
    platform_restart();
}

static void reset_node_db()
{
    app::IAppFacade& app_ctx = app::appFacade();
    app_ctx.clearNodeDb();
    prefs_clear_ns("chat_pki");
    prefs_clear_ns("mc_peers");
    prefs_clear_ns("lxmf_peers");
    ::ui::feedback::show_notice(::ui::i18n::tr("Node DB reset"), 3000);
}

static void clear_message_db()
{
    app::IAppFacade& app_ctx = app::appFacade();
    app_ctx.clearMessageDb();
    ::ui::feedback::show_notice(::ui::i18n::tr("Message DB cleared"), 3000);
}

static bool perform_factory_reset()
{
#if defined(ARDUINO_ARCH_ESP32)
    const app::AppConfig& config = app::appFacade().readConfig();
    if (!::platform::ui::reticulum_network_config::reset(config.reticulumConfig()))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Unable to reset Reticulum configuration"), 3500);
        return false;
    }
    if (!::platform::ui::settings_reset::begin())
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Unable to reset configuration"), 3500);
        return false;
    }
#endif
    static const char* kNamespacesToClear[] = {
        "chat",
        "gps",
        "settings",
        "aprs",
        "power",
        "a7682e",
        "chat_pki",
        "mc_peers",
        "lxmf_peers",
    };

    for (const char* ns : kNamespacesToClear)
    {
        prefs_clear_ns(ns);
    }

    team::ui::team_ui_snapshot_store().clear();

#if defined(ARDUINO_ARCH_ESP32)
    ::platform::ui::settings_reset::finish();
#endif

    ::ui::feedback::show_notice(::ui::i18n::tr("Resetting..."), 1500);
    platform_delay_ms(300);
    platform_restart();
    return true;
}

static void refresh_reticulum_tcp_fields()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
    const auto& network = ::platform::ui::reticulum_network_config::active();
    size_t ordinal = 0;
    g_settings.rt_wifi_gateway_host[0] = '\0';
    copy_bounded(g_settings.rt_wifi_gateway_port, sizeof(g_settings.rt_wifi_gateway_port), "4242");
    for (size_t i = 0; i < network.interface_count; ++i)
    {
        const auto& entry = network.interfaces[i];
        if (entry.type != chat::reticulum::NetworkInterfaceType::TcpClient) continue;
        if (ordinal++ != static_cast<size_t>(g_settings.rt_tcp_slot)) continue;
        copy_bounded(g_settings.rt_wifi_gateway_host, sizeof(g_settings.rt_wifi_gateway_host), entry.target_host);
        std::snprintf(g_settings.rt_wifi_gateway_port, sizeof(g_settings.rt_wifi_gateway_port), "%u", static_cast<unsigned>(entry.target_port));
        break;
    }
#endif
}

static bool save_reticulum_tcp_fields()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
    char* end = nullptr;
    const long port = std::strtol(g_settings.rt_wifi_gateway_port, &end, 10);
    if (!end || *end || port < 1 || port > 65535 ||
        !::platform::ui::reticulum_network_config::updateTcpEndpoint(
            static_cast<size_t>(g_settings.rt_tcp_slot), g_settings.rt_wifi_gateway_host, static_cast<uint16_t>(port)))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Invalid TCP entry; fill earlier slots first"), 3500);
        refresh_reticulum_tcp_fields();
        return false;
    }
    auto& app_ctx = app::appFacade();
    app_ctx.requestSaveConfig(app::AppConfigChangeSet::mesh());
    app_ctx.applyMeshConfig();
    refresh_reticulum_tcp_fields();
    return true;
#else
    return false;
#endif
}

static void settings_load()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "settings_load begin");
#endif
    bind_dynamic_option_storage_to_items();
    auto& options = dynamic_options();
    app::IAppFacade& app_ctx = app::appFacade();
    g_settings.chat_protocol = static_cast<int>(app_ctx.readConfig().mesh_protocol);

    if (kChatRegionOptionCount == 0)
    {
        size_t region_count = 0;
        const chat::meshtastic::RegionInfo* regions = chat::meshtastic::getRegionTable(&region_count);
        size_t limit = kChatRegionOptionCapacity;
        kChatRegionOptionCount = (region_count < limit) ? region_count : limit;
        for (size_t i = 0; i < kChatRegionOptionCount; ++i)
        {
            options.chat_region_options[i].label = regions[i].label;
            options.chat_region_options[i].value = regions[i].code;
        }
    }
    if (kMeshCoreRegionPresetOptionCount == 0)
    {
        size_t preset_count = 0;
        const chat::meshcore::RegionPreset* presets =
            chat::meshcore::getRegionPresetTable(&preset_count);
        size_t limit = kMeshCoreRegionPresetOptionCapacity;
        if (limit > 0)
        {
            options.meshcore_region_preset_options[0].label = "Custom";
            options.meshcore_region_preset_options[0].value = 0;
            size_t copy_count = preset_count;
            if (copy_count > (limit - 1))
            {
                copy_count = limit - 1;
            }
            for (size_t i = 0; i < copy_count; ++i)
            {
                options.meshcore_region_preset_options[i + 1].label = presets[i].title;
                options.meshcore_region_preset_options[i + 1].value = presets[i].id;
            }
            kMeshCoreRegionPresetOptionCount = copy_count + 1;
        }
    }
    if (kTxPowerOptionCount == 0)
    {
        size_t limit = kTxPowerOptionCapacity;
        int value = kNetTxPowerMin;
        while (value <= kNetTxPowerMax && kTxPowerOptionCount < limit)
        {
            snprintf(options.tx_power_labels[kTxPowerOptionCount],
                     sizeof(options.tx_power_labels[kTxPowerOptionCount]),
                     "%d dBm",
                     value);
            options.tx_power_options[kTxPowerOptionCount].label =
                options.tx_power_labels[kTxPowerOptionCount];
            options.tx_power_options[kTxPowerOptionCount].value = value;
            kTxPowerOptionCount++;
            value++;
        }
    }

    refresh_language_pack_options();

    app_ctx.getEffectiveUserInfo(g_settings.user_name,
                                 sizeof(g_settings.user_name),
                                 g_settings.short_name,
                                 sizeof(g_settings.short_name));
    const app::AppConfig& cfg = app_ctx.readConfig();
    const chat::MeshConfig& mt_cfg = cfg.meshtastic_config;
    const chat::MeshConfig& mc_cfg = cfg.meshcore_config;
    const chat::MeshConfig& reticulum_cfg = cfg.reticulumConfig();

    uint32_t gps_interval_seconds = cfg.gps_interval_ms / 1000U;
    if (gps_interval_seconds == 0)
    {
        gps_interval_seconds = 1;
    }
    g_settings.gps_enabled = cfg.gps_enabled;
    g_settings.gps_init_baud = static_cast<int>(cfg.gps_init_baud);
    g_settings.gps_init_probe_ms = static_cast<int>(cfg.gps_init_probe_ms);
    g_settings.gps_init_profile = cfg.gps_init_profile;
    g_settings.gps_init_rxm_policy = cfg.gps_init_rxm_policy;
    g_settings.gps_init_gnss_policy = cfg.gps_init_gnss_policy;
    g_settings.gps_init_nmea_policy = cfg.gps_init_nmea_policy;
    g_settings.gps_mode = cfg.gps_mode;
    g_settings.gps_sat_mask = cfg.gps_sat_mask;
    g_settings.gps_strategy = cfg.gps_strategy;
    g_settings.gps_interval = static_cast<int>(gps_interval_seconds);
    g_settings.gps_alt_ref = cfg.gps_alt_ref;
    g_settings.gps_coord_format = cfg.gps_coord_format;
    g_settings.external_nmea_output_hz = cfg.external_nmea_output_hz;
    g_settings.external_nmea_sentence_mask = cfg.external_nmea_sentence_mask;

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
    g_settings.chat_message_alerts = prefs_get_int("chat_message_alerts", 1) ? 1 : 0;
    g_settings.chat_contact_alerts = prefs_get_int("chat_contact_alerts", kChatContactAlertsContacts);
    g_settings.chat_auto_reply_enabled = prefs_get_bool(
        ::platform::ui::auto_reply::kEnabledKey, false);
    std::string auto_reply_text;
    if (settings_store::get_string(::platform::ui::auto_reply::kSettingsNamespace,
                                   ::platform::ui::auto_reply::kTextKey,
                                   auto_reply_text))
    {
        copy_bounded(g_settings.chat_auto_reply_text,
                     sizeof(g_settings.chat_auto_reply_text),
                     auto_reply_text.c_str());
    }
    else
    {
        g_settings.chat_auto_reply_text[0] = '\0';
    }
    if (g_settings.chat_contact_alerts < kChatContactAlertsNone ||
        g_settings.chat_contact_alerts > kChatContactAlertsAll)
    {
        g_settings.chat_contact_alerts = kChatContactAlertsContacts;
    }
    ::settings::ui::channel::sync_meshtastic_channel_fields(cfg, g_settings);
    const uint8_t* active_psk = nullptr;
    size_t active_psk_len = 0;
    if (cfg.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshCoreChannelConfig& active_channel =
            mc_cfg.meshCoreChannel(mc_cfg.meshcore_channel_slot);
        active_psk = active_channel.key;
        active_psk_len = chat::kMeshCoreChannelKeyLen;
    }
    else if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        active_psk = mt_cfg.secondary_key;
        active_psk_len = chat::normalizeMeshtasticChannelKeyLen(mt_cfg.secondary_key,
                                                                sizeof(mt_cfg.secondary_key),
                                                                mt_cfg.secondary_key_len);
    }
    if (!active_psk || active_psk_len == 0 ||
        ::settings::ui::channel::is_zero_key(active_psk, active_psk_len))
    {
        g_settings.chat_psk[0] = '\0';
    }
    else
    {
        ::settings::ui::channel::bytes_to_hex(active_psk,
                                              active_psk_len,
                                              g_settings.chat_psk,
                                              sizeof(g_settings.chat_psk));
    }
    refresh_reticulum_identity_fields(app_ctx, cfg);
    refresh_reticulum_tcp_fields();

    if (chat::infra::isReticulumMeshProtocol(cfg.mesh_protocol))
    {
        g_settings.net_use_preset = 0;
        g_settings.net_modem_preset = 0;
        g_settings.net_manual_bw = static_cast<int>(std::lround(reticulum_cfg.bandwidth_khz));
        g_settings.net_manual_sf = reticulum_cfg.spread_factor;
        g_settings.net_manual_cr = reticulum_cfg.coding_rate;
        float_to_text(reticulum_cfg.override_frequency_mhz, g_settings.net_override_freq,
                      sizeof(g_settings.net_override_freq), 3);
        g_settings.rt_bearer_policy =
            reticulum_bearer_policy_to_value(reticulum_cfg.reticulum_interface_policy);
        g_settings.rt_lora_enabled = reticulum_cfg.reticulum_lora_enabled;
        g_settings.rt_wifi_gateway_enabled = reticulum_cfg.reticulum_wifi_gateway_enabled;
        g_settings.rt_wifi_auto_connect = reticulum_cfg.reticulum_wifi_auto_connect;
        g_settings.rt_anonymous_peer = reticulum_cfg.reticulum_anonymous_peer;
        g_settings.rt_location_requests =
            reticulum_cfg.reticulum_allow_location_requests;
        copy_bounded(g_settings.rt_wifi_gateway_host,
                     sizeof(g_settings.rt_wifi_gateway_host),
                     reticulum_cfg.reticulum_wifi_gateway_host);
        std::snprintf(g_settings.rt_wifi_gateway_port,
                      sizeof(g_settings.rt_wifi_gateway_port),
                      "%u",
                      static_cast<unsigned>(
                          reticulum_cfg.reticulum_wifi_gateway_port != 0
                              ? reticulum_cfg.reticulum_wifi_gateway_port
                              : 4242));
        refresh_reticulum_tcp_fields();
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
    g_settings.mt_mqtt_enabled = cfg.meshtastic_mqtt_enabled;
    g_settings.mt_mqtt_uplink = cfg.meshtastic_mqtt_uplink_enabled;
    g_settings.mt_mqtt_downlink = cfg.meshtastic_mqtt_downlink_enabled;
    copy_bounded(g_settings.mt_mqtt_host,
                 sizeof(g_settings.mt_mqtt_host),
                 cfg.meshtastic_mqtt_host);
    std::snprintf(g_settings.mt_mqtt_port,
                  sizeof(g_settings.mt_mqtt_port),
                  "%u",
                  static_cast<unsigned>(
                      cfg.meshtastic_mqtt_port != 0
                          ? cfg.meshtastic_mqtt_port
                          : 1883));
    copy_bounded(g_settings.mt_mqtt_root,
                 sizeof(g_settings.mt_mqtt_root),
                 cfg.meshtastic_mqtt_root[0]
                     ? cfg.meshtastic_mqtt_root
                     : app::AppConfig::kDefaultMeshtasticMqttRoot);
    copy_bounded(g_settings.mt_mqtt_user,
                 sizeof(g_settings.mt_mqtt_user),
                 cfg.meshtastic_mqtt_username);
    copy_bounded(g_settings.mt_mqtt_pass,
                 sizeof(g_settings.mt_mqtt_pass),
                 cfg.meshtastic_mqtt_password);

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
    g_settings.mc_send_profile = static_cast<int>(static_cast<uint8_t>(mc_cfg.meshcore_send_profile));
    g_settings.mc_forward_profile = static_cast<int>(static_cast<uint8_t>(mc_cfg.meshcore_forward_profile));
    g_settings.mc_channel_slot =
        chat::normalizeMeshCoreChannelSlot(mc_cfg.meshcore_channel_slot);
    ::settings::ui::channel::sync_meshcore_channel_fields(cfg, g_settings);
    g_settings.mc_mqtt_enabled = mc_cfg.meshcore_mqtt_enabled;
    g_settings.mc_mqtt_uplink = mc_cfg.meshcore_mqtt_uplink_enabled;
    g_settings.mc_mqtt_downlink = mc_cfg.meshcore_mqtt_downlink_enabled;
    copy_bounded(g_settings.mc_mqtt_host,
                 sizeof(g_settings.mc_mqtt_host),
                 mc_cfg.meshcore_mqtt_host);
    std::snprintf(g_settings.mc_mqtt_port,
                  sizeof(g_settings.mc_mqtt_port),
                  "%u",
                  static_cast<unsigned>(
                      mc_cfg.meshcore_mqtt_port != 0
                          ? mc_cfg.meshcore_mqtt_port
                          : 1883));
    copy_bounded(g_settings.mc_mqtt_root,
                 sizeof(g_settings.mc_mqtt_root),
                 mc_cfg.meshcore_mqtt_root[0]
                     ? mc_cfg.meshcore_mqtt_root
                     : app::AppConfig::kDefaultMeshCoreMqttRoot);
    copy_bounded(g_settings.mc_mqtt_user,
                 sizeof(g_settings.mc_mqtt_user),
                 mc_cfg.meshcore_mqtt_username);
    copy_bounded(g_settings.mc_mqtt_pass,
                 sizeof(g_settings.mc_mqtt_pass),
                 mc_cfg.meshcore_mqtt_password);

    g_settings.privacy_encrypt_mode = cfg.privacy_encrypt_mode;

    g_settings.screen_timeout_ms = static_cast<int>(screen_runtime::timeout_ms());
    g_settings.screen_brightness = clamp_screen_brightness(
        prefs_get_int("screen_brightness", static_cast<int>(device_runtime::screen_brightness())));
    g_settings.timezone_offset_min = ::platform::ui::time::timezone_offset_min();
    g_settings.timezone_profile_id = ::platform::ui::time::timezone_profile_id();
    append_custom_timezone_option_if_needed(g_settings.timezone_profile_id, g_settings.timezone_offset_min);
    refresh_manual_time_fields_from_runtime();
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

    g_settings.vibration_enabled = prefs_get_bool("vibration_enabled", true);
    refresh_wifi_state_from_runtime();
    refresh_firmware_update_state_from_runtime();
    refresh_wireless_companion_state_from_runtime();

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
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag,
             "settings_load complete locales=%u tx_power_options=%u wifi_supported=%d wifi_networks=%u",
             static_cast<unsigned>(kLocaleOptionCount),
             static_cast<unsigned>(kTxPowerOptionCount),
             wifi_runtime::is_supported() ? 1 : 0,
             static_cast<unsigned>(kWifiNetworkOptionCount));
#endif
}

static void refresh_language_pack_options()
{
    auto& options = dynamic_options();
    kLocaleOptionCount = 0;
    const size_t locale_limit = kLocaleOptionCapacity;
    for (size_t index = 0; index < ::ui::i18n::locale_count() && kLocaleOptionCount < locale_limit; ++index)
    {
        const ::ui::i18n::LocaleInfo* locale = ::ui::i18n::locale_at(index);
        if (!locale)
        {
            continue;
        }

        const char* display_name =
            (locale->native_name && locale->native_name[0] != '\0') ? locale->native_name : locale->display_name;
        std::snprintf(options.locale_option_labels[kLocaleOptionCount],
                      sizeof(options.locale_option_labels[kLocaleOptionCount]),
                      "%s",
                      display_name ? display_name : "");
        options.locale_options[kLocaleOptionCount].label =
            options.locale_option_labels[kLocaleOptionCount];
        options.locale_options[kLocaleOptionCount].value = static_cast<int>(index);
        ++kLocaleOptionCount;
    }
    g_settings.display_locale_index = ::ui::i18n::current_locale_index();
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
        if (item_id(item) == settings::ui::SettingId::EnabledImes)
        {
            format_enabled_ime_summary(out, out_len);
        }
        else if (item_id(item) == settings::ui::SettingId::ManualTimeSet)
        {
            format_manual_datetime_summary(out, out_len);
        }
        else if (item_id(item) == settings::ui::SettingId::C6EnterDownload)
        {
            snprintf(out, out_len, "%s", ::ui::i18n::tr("Enter"));
        }
        else
        {
            snprintf(out, out_len, "%s", ::ui::i18n::tr("Run"));
        }
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
    const settings::ui::SettingId id = item_id(*widget.def);
    const bool text_value_uses_content_font =
        widget.def->type == settings::ui::SettingType::Text &&
        ::ui::fonts::utf8_has_non_ascii(value);
    const bool use_content_font =
        text_value_uses_content_font ||
        option_labels_use_content_font(*widget.def) ||
        (id == settings::ui::SettingId::EnabledImes &&
         ::ui::fonts::utf8_has_non_ascii(value));
    if (use_content_font)
    {
        ::ui::i18n::set_content_label_text_raw(widget.value_label, value);
    }
    else
    {
        ::ui::i18n::set_label_text_raw(widget.value_label, value);
    }

    if (widget.def->enum_value)
    {
        apply_locale_preview_font(widget.value_label, *widget.def, *widget.def->enum_value);
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
    // The visible header belongs to the active modal's navigation scope.
    if (g_state.top_bar.back_btn)
    {
        lv_group_add_obj(g_state.modal_group, g_state.top_bar.back_btn);
    }
    lv_group_set_editing(g_state.modal_group, false);
}

static void modal_restore_group(bool restore_focus)
{
    if (g_state.modal_group)
    {
        lv_group_del(g_state.modal_group);
        g_state.modal_group = nullptr;
    }
    lv_group_t* previous_group = s_modal_prev_group;
    s_modal_prev_group = nullptr;
    if (restore_focus && previous_group)
    {
        set_default_group(previous_group);
    }
    if (restore_focus)
    {
        settings::ui::input::on_ui_refreshed();
    }
}

static void modal_close(bool restore_focus = true)
{
    if (s_text_modal_ime)
    {
        s_text_modal_ime->detach();
        s_text_modal_ime.reset();
    }
    if (g_state.modal_root)
    {
        lv_obj_del_async(g_state.modal_root);
        g_state.modal_root = nullptr;
    }
    g_state.modal_textarea = nullptr;
    g_state.modal_error = nullptr;
    g_state.editing_item = nullptr;
    g_state.editing_widget = nullptr;
    s_gps_diagnostics_label = nullptr;
    s_spi_diagnostics_label = nullptr;
    for (auto& roller : s_manual_time_rollers)
    {
        roller = nullptr;
    }
    for (auto& obj : s_manual_datetime_focus_order)
    {
        obj = nullptr;
    }
    s_manual_datetime_focus_count = 0;
    s_option_click_count = 0;
    s_ime_toggle_count = 0;
    modal_restore_group(restore_focus);
}

static void on_modal_key(lv_event_t* e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC)
    {
        modal_close();
        lv_event_stop_processing(e);
    }
}

static void modal_add_focus_obj(lv_obj_t* obj)
{
    if (!obj || !g_state.modal_group)
    {
        return;
    }
    lv_group_add_obj(g_state.modal_group, obj);
    lv_obj_remove_event_cb(obj, on_modal_key);
    lv_obj_add_event_cb(obj, on_modal_key, LV_EVENT_KEY, nullptr);
}

static void reset_manual_datetime_focus_order()
{
    for (auto& obj : s_manual_datetime_focus_order)
    {
        obj = nullptr;
    }
    s_manual_datetime_focus_count = 0;
}

static void add_manual_datetime_focus_obj(lv_obj_t* obj)
{
    if (obj == nullptr || s_manual_datetime_focus_count >= kManualDateTimeFocusCapacity)
    {
        return;
    }
    s_manual_datetime_focus_order[s_manual_datetime_focus_count++] = obj;
}

static bool focus_manual_datetime_neighbor(lv_obj_t* current, int delta)
{
    if (current == nullptr || s_manual_datetime_focus_count == 0)
    {
        return false;
    }

    for (size_t index = 0; index < s_manual_datetime_focus_count; ++index)
    {
        if (s_manual_datetime_focus_order[index] != current)
        {
            continue;
        }
        const int count = static_cast<int>(s_manual_datetime_focus_count);
        int next = (static_cast<int>(index) + delta) % count;
        if (next < 0)
        {
            next += count;
        }
        lv_obj_t* target = s_manual_datetime_focus_order[static_cast<size_t>(next)];
        if (target == nullptr)
        {
            return false;
        }
        lv_group_focus_obj(target);
        lv_obj_scroll_to_view(target, LV_ANIM_OFF);
        return true;
    }
    return false;
}

static void on_manual_datetime_focus_key(lv_event_t* e)
{
    if (e == nullptr || lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT)
    {
        return;
    }

    lv_obj_t* target = lv_event_get_target_obj(e);
    if (focus_manual_datetime_neighbor(target, key == LV_KEY_RIGHT ? 1 : -1))
    {
        lv_event_stop_processing(e);
    }
}

static void modal_add_datetime_focus_obj(lv_obj_t* obj)
{
    modal_add_focus_obj(obj);
    add_manual_datetime_focus_obj(obj);
    lv_obj_remove_event_cb(obj, on_manual_datetime_focus_key);
    lv_obj_add_event_cb(obj, on_manual_datetime_focus_key, LV_EVENT_KEY, nullptr);
}

static void on_text_modal_key(lv_event_t* e)
{
    if (s_text_modal_ime && s_text_modal_ime->handle_key(e))
    {
        return;
    }
}

static void on_password_visibility_clicked(lv_event_t* e)
{
    if (e == nullptr)
    {
        return;
    }

    lv_obj_t* textarea = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (textarea == nullptr || !lv_obj_is_valid(textarea))
    {
        return;
    }

    const bool masked = lv_textarea_get_password_mode(textarea);
    lv_textarea_set_password_mode(textarea, !masked);

    lv_obj_t* button = lv_event_get_target_obj(e);
    lv_obj_t* label = button ? lv_obj_get_child(button, 0) : nullptr;
    if (label != nullptr)
    {
        lv_label_set_text(label, masked ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }

    // A keyboard-triggered toggle must return immediately to the editor so
    // the next character cannot activate another modal control.
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_group_focus_obj(textarea);
}

static bool text_modal_hardware_keyboard_available()
{
#if UI_SHARED_TOUCH_IME_ENABLED
    return lv_get_keyboard_indev() != nullptr;
#else
    return false;
#endif
}

static bool should_use_touch_text_modal_layout(const ::ui::page_profile::PageLayoutProfile& profile)
{
    return profile.large_touch_hitbox && profile.ime_keyboard_height > 0 &&
           !text_modal_hardware_keyboard_available();
}

static lv_coord_t resolve_text_modal_ime_host_height(
    const ::ui::page_profile::PageLayoutProfile& profile,
    bool touch_ime_layout)
{
    if (!touch_ime_layout)
    {
        return 24;
    }
    return profile.ime_bar_height + profile.ime_candidate_button_height +
           profile.ime_keyboard_height + 16;
}

static lv_coord_t resolve_text_modal_button_height(
    const ::ui::page_profile::PageLayoutProfile& profile,
    bool touch_ime_layout)
{
    return touch_ime_layout ? profile.control_button_height
                            : ::ui::page_profile::resolve_control_button_height();
}

static lv_coord_t resolve_text_modal_width(
    const ::ui::page_profile::PageLayoutProfile& profile,
    bool touch_ime_layout)
{
    if (!touch_ime_layout)
    {
        return 300;
    }
    return profile.ime_keyboard_height <= 220 ? 760 : 560;
}

static lv_coord_t resolve_text_modal_height(
    const ::ui::page_profile::PageLayoutProfile& profile,
    bool touch_ime_layout)
{
    if (!touch_ime_layout)
    {
        return 204;
    }
    if (profile.ime_keyboard_height > 220)
    {
        return 520;
    }
    const lv_coord_t title_block_height = 24;
    const lv_coord_t textarea_height = 36;
    const lv_coord_t gap_after_textarea = 8;
    const lv_coord_t gap_before_buttons = 10;
    return profile.modal_pad * 2 + title_block_height + textarea_height +
           gap_after_textarea +
           resolve_text_modal_ime_host_height(profile, touch_ime_layout) +
           gap_before_buttons +
           resolve_text_modal_button_height(profile, touch_ime_layout);
}

static lv_obj_t* create_modal_root(lv_coord_t width, lv_coord_t height)
{
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t top_bar_height = profile.top_bar_height > 0 ? profile.top_bar_height
                                                                 : static_cast<lv_coord_t>(::ui::widgets::kTopBarHeight);
    lv_obj_update_layout(g_state.root);
    const lv_coord_t content_height = std::max<lv_coord_t>(1, lv_obj_get_height(g_state.root) - top_bar_height);
    lv_obj_t* bg = lv_obj_create(g_state.root);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(bg, LV_PCT(100), content_height);
    lv_obj_set_pos(bg, 0, top_bar_height);
    style::apply_modal_bg(bg);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    const auto modal_size = ::ui::page_profile::resolve_modal_size(width, height, lv_screen_active());
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, modal_size.width, std::min<lv_coord_t>(modal_size.height, content_height));
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
        const settings::ui::SettingId id = item_id(*g_state.editing_item);
        bool is_user_name = id == settings::ui::SettingId::ChatUser;
        bool is_short_name = id == settings::ui::SettingId::ChatShort;
        update_item_value(*g_state.editing_widget);
        bool broadcast_nodeinfo = false;
        if (is_user_name)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::identity(),
                [&](app::AppConfig& config)
                {
                    strncpy(config.node_name,
                            g_state.editing_item->text_value,
                            sizeof(config.node_name) - 1);
                    config.node_name[sizeof(config.node_name) - 1] = '\0';
                });
            app_ctx.applyUserInfo();
            broadcast_nodeinfo = true;
        }
        if (is_short_name)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::identity(),
                [&](app::AppConfig& config)
                {
                    strncpy(config.short_name,
                            g_state.editing_item->text_value,
                            sizeof(config.short_name) - 1);
                    config.short_name[sizeof(config.short_name) - 1] = '\0';
                });
            app_ctx.applyUserInfo();
            broadcast_nodeinfo = true;
        }
        if (id == settings::ui::SettingId::ChatAutoReplyText)
        {
            (void)settings_store::put_string(
                ::platform::ui::auto_reply::kSettingsNamespace,
                ::platform::ui::auto_reply::kTextKey,
                g_state.editing_item->text_value);
        }
        if (broadcast_nodeinfo)
        {
            app::appFacade().broadcastNodeInfo();
        }
        if (id == settings::ui::SettingId::MtPrimaryName)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::channels(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshtastic_config.primary_channel_name,
                                 sizeof(config.meshtastic_config.primary_channel_name),
                                 g_state.editing_item->text_value);
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::MtSecondaryName)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::channels(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshtastic_config.secondary_channel_name,
                                 sizeof(config.meshtastic_config.secondary_channel_name),
                                 g_state.editing_item->text_value);
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::MtPrimaryKey)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            auto edit = app_ctx.beginConfigEdit();
            if (!edit ||
                !::settings::ui::channel::parse_meshtastic_key_text(
                    g_state.editing_item->text_value,
                    edit.config().meshtastic_config.primary_key,
                    sizeof(edit.config().meshtastic_config.primary_key),
                    &edit.config().meshtastic_config.primary_key_len))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("PSK must be 32/64 hex or 16/32 chars"), 4000);
                modal_close();
                return;
            }
            edit.commit(app::AppConfigChangeSet::channels());
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::MtSecondaryKey)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            auto edit = app_ctx.beginConfigEdit();
            if (!edit ||
                !::settings::ui::channel::parse_meshtastic_key_text(
                    g_state.editing_item->text_value,
                    edit.config().meshtastic_config.secondary_key,
                    sizeof(edit.config().meshtastic_config.secondary_key),
                    &edit.config().meshtastic_config.secondary_key_len))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("PSK must be 32/64 hex or 16/32 chars"), 4000);
                modal_close();
                return;
            }
            edit.commit(app::AppConfigChangeSet::channels());
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::ChatPsk)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            uint8_t key[chat::kMeshtasticChannelKeyMaxLen] = {};
            size_t parsed_key_len = 0;
            const size_t key_capacity =
                (app_ctx.readConfig().mesh_protocol == chat::MeshProtocol::MeshCore)
                    ? chat::kMeshCoreChannelKeyLen
                    : chat::kMeshtasticChannelKeyMaxLen;
            if (!::settings::ui::channel::parse_psk(g_state.editing_item->text_value,
                                                    key,
                                                    key_capacity,
                                                    &parsed_key_len))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("PSK must be 32/64 hex or 16/32 chars"), 4000);
                modal_close();
                return;
            }
            auto edit = app_ctx.beginConfigEdit();
            if (!edit)
            {
                modal_close();
                return;
            }
            if (edit.config().mesh_protocol == chat::MeshProtocol::MeshCore)
            {
                chat::MeshConfig& mesh = edit.config().meshcore_config;
                const uint8_t slot =
                    chat::normalizeMeshCoreChannelSlot(mesh.meshcore_channel_slot);
                chat::MeshCoreChannelConfig& channel = mesh.meshCoreChannel(slot);
                memset(channel.key, 0, sizeof(channel.key));
                memcpy(channel.key, key, chat::kMeshCoreChannelKeyLen);
                if (slot != 0)
                {
                    channel.enabled = true;
                }
                mesh.syncMeshCoreLegacyChannelMirror();
            }
            else
            {
                auto& mesh = edit.config().meshtastic_config;
                memset(mesh.secondary_key, 0, sizeof(mesh.secondary_key));
                memcpy(mesh.secondary_key, key, parsed_key_len);
                mesh.secondary_key_len =
                    chat::normalizeMeshtasticChannelKeyLen(mesh.secondary_key,
                                                           sizeof(mesh.secondary_key),
                                                           static_cast<uint8_t>(parsed_key_len));
            }
            edit.commit(app::AppConfigChangeSet::channels());
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::NetFreqOffset)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid frequency offset"), 3000);
                modal_close();
                return;
            }
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshtastic_config.frequency_offset_mhz = value;
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::NetOverrideFreq)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid frequency value"), 3000);
                modal_close();
                return;
            }
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    if (chat::infra::isReticulumMeshProtocol(config.mesh_protocol))
                    {
                        config.reticulumConfig().override_frequency_mhz = value;
                    }
                    else
                    {
                        config.meshtastic_config.override_frequency_mhz = value;
                    }
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::McFreq)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid MeshCore frequency"), 3000);
                modal_close();
                return;
            }
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshcore_config.meshcore_freq_mhz = value;
                    config.meshcore_config.meshcore_region_preset = 0;
                });
            g_settings.mc_region_preset = 0;
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::McBw)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid MeshCore bandwidth"), 3000);
                modal_close();
                return;
            }
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshcore_config.meshcore_bw_khz = value;
                    config.meshcore_config.meshcore_region_preset = 0;
                });
            g_settings.mc_region_preset = 0;
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::McRxDelay)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid RX delay"), 3000);
                modal_close();
                return;
            }
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshcore_config.meshcore_rx_delay_base = value;
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::McAirtime)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            float value = 0.0f;
            if (!parse_float_text(g_state.editing_item->text_value, &value))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid airtime factor"), 3000);
                modal_close();
                return;
            }
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshcore_config.meshcore_airtime_factor = value;
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::McChannelName)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            const uint8_t slot =
                chat::normalizeMeshCoreChannelSlot(static_cast<uint8_t>(g_settings.mc_channel_slot));
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::channels(),
                [&](app::AppConfig& config)
                {
                    chat::MeshConfig& mesh = config.meshcore_config;
                    chat::MeshCoreChannelConfig& channel = mesh.meshCoreChannel(slot);
                    copy_bounded(channel.name,
                                 sizeof(channel.name),
                                 g_state.editing_item->text_value);
                    if (slot != 0 && channel.name[0] != '\0')
                    {
                        channel.enabled = true;
                    }
                    mesh.meshcore_channel_slot = slot;
                    mesh.syncMeshCoreLegacyChannelMirror();
                    ::settings::ui::channel::sync_meshcore_channel_fields(config,
                                                                          g_settings);
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::McChannelKey)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            uint8_t key[16] = {};
            if (!::settings::ui::channel::parse_psk(g_state.editing_item->text_value,
                                                    key,
                                                    sizeof(key)))
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Key must be 32 hex or 16 chars"), 3000);
                modal_close();
                return;
            }
            const uint8_t slot =
                chat::normalizeMeshCoreChannelSlot(static_cast<uint8_t>(g_settings.mc_channel_slot));
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::channels(),
                [&](app::AppConfig& config)
                {
                    chat::MeshConfig& mesh = config.meshcore_config;
                    chat::MeshCoreChannelConfig& channel = mesh.meshCoreChannel(slot);
                    memcpy(channel.key, key, sizeof(key));
                    if (slot != 0)
                    {
                        channel.enabled = true;
                    }
                    mesh.meshcore_channel_slot = slot;
                    mesh.syncMeshCoreLegacyChannelMirror();
                    ::settings::ui::channel::sync_meshcore_channel_fields(config,
                                                                          g_settings);
                });
            app_ctx.applyMeshConfig();
        }
        if (id == settings::ui::SettingId::GaugeDesignMah)
        {
            // Update gauge design capacity (mAh) in the shared "power" settings namespace.
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') || value <= 0 || value > 10000)
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid design capacity (mAh)"), 3000);
                modal_close();
                return;
            }
            prefs_put_uint_ns("power", "gauge_design_mah", static_cast<uint32_t>(value));
            device_runtime::reload_configurable_battery_gauge();
        }
        if (id == settings::ui::SettingId::GaugeFullMah)
        {
            // Update gauge full-charge capacity (mAh) that shares the same profile with design capacity.
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') || value <= 0 || value > 10000)
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid full capacity (mAh)"), 3000);
                modal_close();
                return;
            }
            prefs_put_uint_ns("power", "gauge_full_mah", static_cast<uint32_t>(value));
            device_runtime::reload_configurable_battery_gauge();
        }
        if (id == settings::ui::SettingId::RtWifiHost)
        {
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
            save_reticulum_tcp_fields();
            refresh_visible_item_values();
#else
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(
                        config.reticulumConfig().reticulum_wifi_gateway_host,
                        sizeof(config.reticulumConfig().reticulum_wifi_gateway_host),
                        g_state.editing_item->text_value);
                });
            app_ctx.applyMeshConfig();
#endif
        }
        if (id == settings::ui::SettingId::RtWifiPort)
        {
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
            save_reticulum_tcp_fields();
            refresh_visible_item_values();
#else
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') ||
                value <= 0 || value > 65535)
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid gateway port"), 3000);
                modal_close();
                return;
            }
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.reticulumConfig().reticulum_wifi_gateway_port =
                        static_cast<uint16_t>(value);
                });
            app_ctx.applyMeshConfig();
            std::snprintf(g_settings.rt_wifi_gateway_port,
                          sizeof(g_settings.rt_wifi_gateway_port),
                          "%u",
                          static_cast<unsigned>(value));
#endif
        }
        if (id == settings::ui::SettingId::MtMqttHost)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshtastic_mqtt_host,
                                 sizeof(config.meshtastic_mqtt_host),
                                 g_state.editing_item->text_value);
                });
        }
        if (id == settings::ui::SettingId::MtMqttPort)
        {
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') ||
                value <= 0 || value > 65535)
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid MQTT port"), 3000);
                modal_close();
                return;
            }
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshtastic_mqtt_port = static_cast<uint16_t>(value);
                });
            std::snprintf(g_settings.mt_mqtt_port,
                          sizeof(g_settings.mt_mqtt_port),
                          "%u",
                          static_cast<unsigned>(value));
        }
        if (id == settings::ui::SettingId::MtMqttRoot)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            const char* root =
                g_state.editing_item->text_value[0] != '\0'
                    ? g_state.editing_item->text_value
                    : app::AppConfig::kDefaultMeshtasticMqttRoot;
            const bool committed = commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [root](app::AppConfig& config)
                {
                    copy_bounded(config.meshtastic_mqtt_root,
                                 sizeof(config.meshtastic_mqtt_root),
                                 root);
                });
            if (!committed)
            {
                modal_close();
                return;
            }
            copy_bounded(g_settings.mt_mqtt_root, sizeof(g_settings.mt_mqtt_root), root);
            std::printf("[Settings][MQTT] mt root saved root=%s\n", root);
        }
        if (id == settings::ui::SettingId::MtMqttUser)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshtastic_mqtt_username,
                                 sizeof(config.meshtastic_mqtt_username),
                                 g_state.editing_item->text_value);
                });
        }
        if (id == settings::ui::SettingId::MtMqttPass)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshtastic_mqtt_password,
                                 sizeof(config.meshtastic_mqtt_password),
                                 g_state.editing_item->text_value);
                });
        }
        if (id == settings::ui::SettingId::McMqttHost)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshcore_config.meshcore_mqtt_host,
                                 sizeof(config.meshcore_config.meshcore_mqtt_host),
                                 g_state.editing_item->text_value);
                });
        }
        if (id == settings::ui::SettingId::McMqttPort)
        {
            char* end = nullptr;
            long value = strtol(g_state.editing_item->text_value, &end, 10);
            if (end == g_state.editing_item->text_value || (end && *end != '\0') ||
                value <= 0 || value > 65535)
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Invalid MQTT port"), 3000);
                modal_close();
                return;
            }
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [value](app::AppConfig& config)
                {
                    config.meshcore_config.meshcore_mqtt_port =
                        static_cast<uint16_t>(value);
                });
            std::snprintf(g_settings.mc_mqtt_port,
                          sizeof(g_settings.mc_mqtt_port),
                          "%u",
                          static_cast<unsigned>(value));
        }
        if (id == settings::ui::SettingId::McMqttRoot)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            const char* root =
                g_state.editing_item->text_value[0] != '\0'
                    ? g_state.editing_item->text_value
                    : app::AppConfig::kDefaultMeshCoreMqttRoot;
            const bool committed = commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [root](app::AppConfig& config)
                {
                    copy_bounded(config.meshcore_config.meshcore_mqtt_root,
                                 sizeof(config.meshcore_config.meshcore_mqtt_root),
                                 root);
                });
            if (!committed)
            {
                modal_close();
                return;
            }
            copy_bounded(g_settings.mc_mqtt_root, sizeof(g_settings.mc_mqtt_root), root);
        }
        if (id == settings::ui::SettingId::McMqttUser)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshcore_config.meshcore_mqtt_username,
                                 sizeof(config.meshcore_config.meshcore_mqtt_username),
                                 g_state.editing_item->text_value);
                });
        }
        if (id == settings::ui::SettingId::McMqttPass)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            commit_app_config(
                app_ctx,
                app::AppConfigChangeSet::mesh(),
                [&](app::AppConfig& config)
                {
                    copy_bounded(config.meshcore_config.meshcore_mqtt_password,
                                 sizeof(config.meshcore_config.meshcore_mqtt_password),
                                 g_state.editing_item->text_value);
                });
        }
        if (g_state.editing_item->pref_key &&
            (id == settings::ui::SettingId::WifiSsid ||
             id == settings::ui::SettingId::WifiPassword))
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
    const auto& profile = ::ui::page_profile::current();
    const bool touch_ime_layout = should_use_touch_text_modal_layout(profile);
    const lv_coord_t ime_host_height =
        resolve_text_modal_ime_host_height(profile, touch_ime_layout);
    const lv_coord_t button_height =
        resolve_text_modal_button_height(profile, touch_ime_layout);
    g_state.modal_root = create_modal_root(resolve_text_modal_width(profile, touch_ime_layout),
                                           resolve_text_modal_height(profile, touch_ime_layout));
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(win, touch_ime_layout ? 8 : 6, LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text(title, item.label);
    style::apply_label_primary(title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* input_row = nullptr;
    lv_obj_t* input_parent = win;
    if (item.mask_text)
    {
        input_row = lv_obj_create(win);
        lv_obj_set_size(input_row, LV_PCT(100), 36);
        lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(input_row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(input_row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(input_row, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(input_row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);
        input_parent = input_row;
    }

    g_state.modal_textarea = lv_textarea_create(input_parent);
    lv_textarea_set_one_line(g_state.modal_textarea, true);
    lv_textarea_set_max_length(g_state.modal_textarea, static_cast<uint16_t>(item.text_max - 1));
    if (item.mask_text)
    {
        lv_textarea_set_password_mode(g_state.modal_textarea, true);
        lv_obj_set_flex_grow(g_state.modal_textarea, 1);
        lv_obj_set_height(g_state.modal_textarea, LV_PCT(100));
    }
    else
    {
        lv_obj_set_width(g_state.modal_textarea, LV_PCT(100));
        lv_obj_set_height(g_state.modal_textarea, 36);
    }
    lv_obj_set_style_border_width(g_state.modal_textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_state.modal_textarea,
                                  lv_color_hex(kSettingsAmber),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.modal_textarea, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_state.modal_textarea,
                                lv_color_hex(kSettingsText),
                                LV_PART_MAIN);
    if (item.text_value)
    {
        lv_textarea_set_text(g_state.modal_textarea, item.text_value);
        lv_textarea_set_cursor_pos(g_state.modal_textarea, LV_TEXTAREA_CURSOR_LAST);
    }
    lv_obj_add_event_cb(g_state.modal_textarea, on_text_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* password_visibility_btn = nullptr;
    if (item.mask_text)
    {
        password_visibility_btn = lv_btn_create(input_row);
        lv_obj_set_size(password_visibility_btn, 36, 36);
        style::apply_btn_modal(password_visibility_btn);
        lv_obj_set_style_bg_color(password_visibility_btn,
                                  lv_color_hex(kSettingsAmber),
                                  LV_STATE_FOCUSED);
        lv_obj_t* visibility_label = lv_label_create(password_visibility_btn);
        lv_label_set_text(visibility_label, LV_SYMBOL_EYE_OPEN);
        lv_obj_center(visibility_label);
        lv_obj_add_event_cb(password_visibility_btn,
                            on_password_visibility_clicked,
                            LV_EVENT_CLICKED,
                            g_state.modal_textarea);
    }

    lv_obj_t* ime_host = lv_obj_create(win);
    lv_obj_set_width(ime_host, LV_PCT(100));
    lv_obj_set_height(ime_host, ime_host_height);
    lv_obj_set_style_pad_all(ime_host, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ime_host, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ime_host, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ime_host, LV_OBJ_FLAG_SCROLLABLE);

    s_text_modal_ime.reset(new ::ui::widgets::ImeWidget());
    s_text_modal_ime->init(ime_host, g_state.modal_textarea);
    lv_obj_t* ime_toggle = s_text_modal_ime->toggle_btn();
    if (lv_obj_t* toolbar = ime_toggle ? lv_obj_get_parent(ime_toggle) : nullptr)
    {
        lv_obj_t* sym_btn = ::ui::widgets::add_text_candidate_button(
            toolbar,
            g_state.modal_textarea,
            ::ui::widgets::text_candidates::CandidateSet::Symbols,
            g_state.modal_group,
            ime_toggle);
        lv_obj_t* emoji_btn = ::ui::widgets::add_text_candidate_button(
            toolbar,
            g_state.modal_textarea,
            ::ui::widgets::text_candidates::CandidateSet::Emoji,
            g_state.modal_group,
            ime_toggle);
        if (sym_btn)
        {
            lv_obj_move_to_index(sym_btn, 1);
        }
        if (emoji_btn)
        {
            lv_obj_move_to_index(emoji_btn, 2);
        }
    }

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), button_height);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, ::ui::page_profile::resolve_control_button_min_width(), button_height);
    style::apply_btn_modal(save_btn);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(kSettingsAmber), LV_PART_MAIN);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(kSettingsAmberDark), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(save_btn, lv_color_hex(kSettingsAmberDark), LV_PART_MAIN);
    lv_obj_t* save_label = lv_label_create(save_btn);
    ::ui::i18n::set_label_text(save_label, "Save");
    style::apply_label_primary(save_label);
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, on_text_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, ::ui::page_profile::resolve_control_button_min_width(), button_height);
    style::apply_btn_modal(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    style::apply_label_primary(cancel_label);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_text_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    g_state.editing_item = &item;
    g_state.editing_widget = &widget;

    modal_add_focus_obj(g_state.modal_textarea);
    modal_add_focus_obj(password_visibility_btn);
    if (s_text_modal_ime && s_text_modal_ime->focus_obj())
    {
        modal_add_focus_obj(s_text_modal_ime->focus_obj());
    }
    modal_add_focus_obj(save_btn);
    modal_add_focus_obj(cancel_btn);
    lv_group_focus_obj(g_state.modal_textarea);
}

static void build_number_options(char* out,
                                 size_t out_size,
                                 int min_value,
                                 int max_value,
                                 int digits)
{
    if (out == nullptr || out_size == 0 || min_value > max_value)
    {
        return;
    }

    out[0] = '\0';
    size_t used = 0;
    for (int value = min_value; value <= max_value; ++value)
    {
        char item[16] = {};
        if (digits == 4)
        {
            std::snprintf(item, sizeof(item), "%04d", value);
        }
        else
        {
            std::snprintf(item, sizeof(item), "%02d", value);
        }

        const int written = std::snprintf(out + used,
                                          out_size - used,
                                          "%s%s",
                                          value == min_value ? "" : "\n",
                                          item);
        if (written < 0)
        {
            break;
        }
        const size_t count = static_cast<size_t>(written);
        if (count >= out_size - used)
        {
            out[out_size - 1] = '\0';
            break;
        }
        used += count;
    }
}

static int manual_time_field_value(const char* text,
                                   int fallback,
                                   int min_value,
                                   int max_value)
{
    int parsed = 0;
    if (parse_int_range(text, min_value, max_value, parsed))
    {
        return parsed;
    }
    return clamp_int(fallback, min_value, max_value);
}

static int manual_datetime_roller_value(ManualDateTimeField field)
{
    lv_obj_t* roller = s_manual_time_rollers[field];
    if (roller)
    {
        const int selected = static_cast<int>(lv_roller_get_selected(roller));
        switch (field)
        {
        case kManualDateTimeYear:
            return kManualDateTimeYearMin + selected;
        case kManualDateTimeMonth:
        case kManualDateTimeDay:
            return 1 + selected;
        case kManualDateTimeHour:
        case kManualDateTimeMinute:
        case kManualDateTimeSecond:
            return selected;
        default:
            break;
        }
    }

    switch (field)
    {
    case kManualDateTimeYear:
        return manual_time_field_value(g_settings.manual_time_year,
                                       kManualDateTimeYearMin,
                                       kManualDateTimeYearMin,
                                       kManualDateTimeYearMax);
    case kManualDateTimeMonth:
        return manual_time_field_value(g_settings.manual_time_month, 1, 1, 12);
    case kManualDateTimeDay:
        return manual_time_field_value(g_settings.manual_time_day, 1, 1, 31);
    case kManualDateTimeHour:
        return manual_time_field_value(g_settings.manual_time_hour, 0, 0, 23);
    case kManualDateTimeMinute:
        return manual_time_field_value(g_settings.manual_time_minute, 0, 0, 59);
    case kManualDateTimeSecond:
        return manual_time_field_value(g_settings.manual_time_second, 0, 0, 59);
    default:
        return 0;
    }
}

static void sync_manual_day_roller(bool keep_selection)
{
    lv_obj_t* day_roller = s_manual_time_rollers[kManualDateTimeDay];
    if (day_roller == nullptr)
    {
        return;
    }

    const int year = manual_datetime_roller_value(kManualDateTimeYear);
    const int month = manual_datetime_roller_value(kManualDateTimeMonth);
    const int max_day = days_in_month(year, month);
    const int current_day =
        keep_selection
            ? manual_datetime_roller_value(kManualDateTimeDay)
            : clamp_int(manual_time_field_value(g_settings.manual_time_day, 1, 1, 31), 1, max_day);
    const int bounded_day = clamp_int(current_day, 1, max_day);

    build_number_options(s_manual_day_options, sizeof(s_manual_day_options), 1, max_day, 2);
    lv_roller_set_options(day_roller, s_manual_day_options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(day_roller, static_cast<uint32_t>(bounded_day - 1), LV_ANIM_OFF);
}

static void on_manual_datetime_roller_changed(lv_event_t* e)
{
    if (e == nullptr || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }
    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target == s_manual_time_rollers[kManualDateTimeYear] ||
        target == s_manual_time_rollers[kManualDateTimeMonth])
    {
        sync_manual_day_roller(true);
    }
}

static void apply_manual_time_roller_style(lv_obj_t* roller)
{
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_bg_color(roller, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(roller, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(roller, lv_color_black(), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
#else
    lv_obj_set_style_bg_color(roller, lv_color_hex(0xF6E6C6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(roller, lv_color_hex(0xE7C98F), LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(0x6B4A1E), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(0xF6E6C6), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0xEBA341), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
#endif
    lv_obj_set_style_text_font(
        roller, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        roller, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), LV_PART_SELECTED);
    lv_obj_set_scrollbar_mode(roller, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t* create_manual_time_roller(lv_obj_t* parent,
                                           const char* caption,
                                           const char* options,
                                           uint32_t selected,
                                           lv_coord_t width,
                                           lv_coord_t height,
                                           bool adjusts_day)
{
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(col, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(col);
    ::ui::i18n::set_label_text_raw(label, caption);
    style::apply_label_muted(label);

    lv_obj_t* roller = lv_roller_create(col);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
    lv_obj_set_size(roller, width, height);
    apply_manual_time_roller_style(roller);
    if (adjusts_day)
    {
        lv_obj_add_event_cb(roller, on_manual_datetime_roller_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    return roller;
}

static void copy_manual_datetime_modal_fields()
{
    format_fixed_4(g_settings.manual_time_year,
                   sizeof(g_settings.manual_time_year),
                   manual_datetime_roller_value(kManualDateTimeYear));
    format_fixed_2(g_settings.manual_time_month,
                   sizeof(g_settings.manual_time_month),
                   manual_datetime_roller_value(kManualDateTimeMonth));
    format_fixed_2(g_settings.manual_time_day,
                   sizeof(g_settings.manual_time_day),
                   manual_datetime_roller_value(kManualDateTimeDay));
    format_fixed_2(g_settings.manual_time_hour,
                   sizeof(g_settings.manual_time_hour),
                   manual_datetime_roller_value(kManualDateTimeHour));
    format_fixed_2(g_settings.manual_time_minute,
                   sizeof(g_settings.manual_time_minute),
                   manual_datetime_roller_value(kManualDateTimeMinute));
    format_fixed_2(g_settings.manual_time_second,
                   sizeof(g_settings.manual_time_second),
                   manual_datetime_roller_value(kManualDateTimeSecond));
}

static void on_manual_datetime_ok_clicked(lv_event_t* e)
{
    (void)e;
    copy_manual_datetime_modal_fields();
    if (!apply_manual_datetime_setting())
    {
        return;
    }
    if (g_state.editing_widget)
    {
        update_item_value(*g_state.editing_widget);
    }
    modal_close();
}

static void on_manual_datetime_cancel_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
}

static void open_manual_datetime_modal(settings::ui::ItemWidget& widget)
{
    if (g_state.modal_root)
    {
        return;
    }

    refresh_manual_time_fields_from_runtime();
    modal_prepare_group();
    reset_manual_datetime_focus_order();
    const bool dense = ::ui::page_profile::is_dense();
    g_state.modal_root = create_modal_root(dense ? 344 : 368, dense ? 176 : 190);
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(win, dense ? 5 : 7, LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text(title, "Set Date/Time");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    style::apply_label_primary(title);

    const int year = manual_time_field_value(g_settings.manual_time_year,
                                             kManualDateTimeYearMin,
                                             kManualDateTimeYearMin,
                                             kManualDateTimeYearMax);
    const int month = manual_time_field_value(g_settings.manual_time_month, 1, 1, 12);
    const int max_day = days_in_month(year, month);
    const int day = clamp_int(manual_time_field_value(g_settings.manual_time_day, 1, 1, 31),
                              1,
                              max_day);
    const int hour = manual_time_field_value(g_settings.manual_time_hour, 0, 0, 23);
    const int minute = manual_time_field_value(g_settings.manual_time_minute, 0, 0, 59);
    const int second = manual_time_field_value(g_settings.manual_time_second, 0, 0, 59);

    build_number_options(s_manual_year_options,
                         sizeof(s_manual_year_options),
                         kManualDateTimeYearMin,
                         kManualDateTimeYearMax,
                         4);
    build_number_options(s_manual_month_options, sizeof(s_manual_month_options), 1, 12, 2);
    build_number_options(s_manual_day_options, sizeof(s_manual_day_options), 1, max_day, 2);
    build_number_options(s_manual_hour_options, sizeof(s_manual_hour_options), 0, 23, 2);
    build_number_options(s_manual_minute_options, sizeof(s_manual_minute_options), 0, 59, 2);
    build_number_options(s_manual_second_options, sizeof(s_manual_second_options), 0, 59, 2);

    lv_obj_t* picker_row = lv_obj_create(win);
    lv_obj_set_width(picker_row, LV_PCT(100));
    lv_obj_set_height(picker_row, dense ? 78 : 88);
    lv_obj_set_flex_flow(picker_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(picker_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(picker_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(picker_row, dense ? 3 : 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(picker_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(picker_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(picker_row, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t roller_height = dense ? 54 : 64;
    const lv_coord_t year_width = dense ? 62 : 70;
    const lv_coord_t field_width = dense ? 40 : 46;
    s_manual_time_rollers[kManualDateTimeYear] =
        create_manual_time_roller(picker_row,
                                  "YYYY",
                                  s_manual_year_options,
                                  static_cast<uint32_t>(year - kManualDateTimeYearMin),
                                  year_width,
                                  roller_height,
                                  true);
    s_manual_time_rollers[kManualDateTimeMonth] =
        create_manual_time_roller(picker_row,
                                  "MM",
                                  s_manual_month_options,
                                  static_cast<uint32_t>(month - 1),
                                  field_width,
                                  roller_height,
                                  true);
    s_manual_time_rollers[kManualDateTimeDay] =
        create_manual_time_roller(picker_row,
                                  "DD",
                                  s_manual_day_options,
                                  static_cast<uint32_t>(day - 1),
                                  field_width,
                                  roller_height,
                                  false);
    s_manual_time_rollers[kManualDateTimeHour] =
        create_manual_time_roller(picker_row,
                                  "HH",
                                  s_manual_hour_options,
                                  static_cast<uint32_t>(hour),
                                  field_width,
                                  roller_height,
                                  false);
    s_manual_time_rollers[kManualDateTimeMinute] =
        create_manual_time_roller(picker_row,
                                  "MM",
                                  s_manual_minute_options,
                                  static_cast<uint32_t>(minute),
                                  field_width,
                                  roller_height,
                                  false);
    s_manual_time_rollers[kManualDateTimeSecond] =
        create_manual_time_roller(picker_row,
                                  "SS",
                                  s_manual_second_options,
                                  static_cast<uint32_t>(second),
                                  field_width,
                                  roller_height,
                                  false);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ok_btn = lv_btn_create(btn_row);
    style::apply_btn_modal(ok_btn);
    lv_obj_set_size(ok_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* ok_label = lv_label_create(ok_btn);
    ::ui::i18n::set_label_text(ok_label, "OK");
    lv_obj_center(ok_label);
    lv_obj_add_event_cb(ok_btn, on_manual_datetime_ok_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    style::apply_btn_modal(cancel_btn);
    lv_obj_set_size(cancel_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_manual_datetime_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    g_state.editing_widget = &widget;
    for (lv_obj_t* roller : s_manual_time_rollers)
    {
        if (roller)
        {
            modal_add_datetime_focus_obj(roller);
        }
    }
    modal_add_datetime_focus_obj(ok_btn);
    modal_add_datetime_focus_obj(cancel_btn);
    if (s_manual_time_rollers[kManualDateTimeYear])
    {
        lv_group_focus_obj(s_manual_time_rollers[kManualDateTimeYear]);
    }
}

void on_locale_change_completed(bool changed, void* /*user_data*/)
{
    g_settings.display_locale_index = ::ui::i18n::current_locale_index();
    if (changed)
    {
        ::ui::menu_layout::refresh_localized_text();
    }

    // The locale change may replace every settings label. Rebuild after the
    // post-frame font transaction completes rather than from the input event
    // that requested it; this also restores the prior option on failure.
    ui_request_rebuild_active_app();
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
    const settings::ui::SettingId id = item_id(*payload->item);
    *payload->item->enum_value = payload->value;
    if (::settings::ui::spec::is_settings_store_owned_enum(id))
    {
        prefs_put_int(payload->item->pref_key, payload->value);
    }
    update_item_value(*payload->widget);
    if (id == settings::ui::SettingId::RtTcpSlot)
    {
        refresh_reticulum_tcp_fields();
        refresh_visible_item_values();
        modal_close();
        return;
    }
    if (id == settings::ui::SettingId::DisplayLocale)
    {
        if (!::ui::i18n::request_locale_by_index(
                static_cast<size_t>(payload->value),
                true,
                on_locale_change_completed,
                nullptr))
        {
            *payload->item->enum_value = previous_value;
            update_item_value(*payload->widget);
        }
        return;
    }
    if (id == settings::ui::SettingId::WifiNetwork)
    {
        if (payload->value >= 0 &&
            static_cast<size_t>(payload->value) < kWifiNetworkOptionCount)
        {
            const wifi_runtime::ScanResult& result =
                dynamic_options().wifi_scan_results[static_cast<size_t>(payload->value)];
            copy_bounded(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), result.ssid);
            wifi_runtime::Config saved_config{};
            if (wifi_runtime::find_saved_config(g_settings.wifi_ssid, saved_config))
            {
                copy_bounded(g_settings.wifi_password,
                             sizeof(g_settings.wifi_password),
                             saved_config.password);
                saved_config.enabled = g_settings.wifi_enabled;
                (void)wifi_runtime::save_config(saved_config);
                refresh_wifi_state_from_runtime();
            }
            else
            {
                g_settings.wifi_password[0] = '\0';
            }
            refresh_visible_item_values();
            rebuild_list = true;
        }
    }
    if (id == settings::ui::SettingId::RtBearer)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [&](app::AppConfig& config)
            {
                chat::MeshConfig& reticulum = config.reticulumConfig();
                apply_reticulum_bearer_policy(
                    reticulum,
                    reticulum_bearer_policy_from_value(payload->value));
                g_settings.rt_bearer_policy =
                    reticulum_bearer_policy_to_value(
                        reticulum.reticulum_interface_policy);
                g_settings.rt_lora_enabled = reticulum.reticulum_lora_enabled;
                g_settings.rt_wifi_gateway_enabled =
                    reticulum.reticulum_wifi_gateway_enabled;
            });
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::MeshProtocol)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        chat::MeshProtocol target = chat::infra::normalizeMeshProtocol(
            static_cast<chat::MeshProtocol>(payload->value));
        if (!app_ctx.switchMeshProtocol(target, true))
        {
            *payload->item->enum_value = previous_value;
            update_item_value(*payload->widget);
            ::ui::feedback::show_notice(::ui::i18n::tr("Protocol switch failed"), 3000);
        }
        else
        {
            settings_load();
            rebuild_list = true;
            ::ui::feedback::show_notice(::ui::i18n::tr("Protocol switched"), 2000);
        }
    }
    if (id == settings::ui::SettingId::ChatRegion)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [&](app::AppConfig& config)
            {
                chat::MeshConfig& mt_cfg = config.meshtastic_config;
                mt_cfg.region = static_cast<uint8_t>(payload->value);
                const auto* region = chat::meshtastic::findRegion(
                    static_cast<meshtastic_Config_LoRaConfig_RegionCode>(
                        mt_cfg.region));
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
            });
        app_ctx.applyMeshConfig();
        app_ctx.applyNetworkLimits();
    }
    if (id == settings::ui::SettingId::NetUsePreset)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshtastic_config.use_preset = (payload->value != 0);
            });
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::NetPreset)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshtastic_config.modem_preset =
                    static_cast<uint8_t>(payload->value);
                config.meshtastic_config.use_preset = true;
            });
        g_settings.net_use_preset = true;
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::NetBw)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                if (chat::infra::isReticulumMeshProtocol(config.mesh_protocol))
                {
                    config.reticulumConfig().bandwidth_khz =
                        static_cast<float>(payload->value);
                }
                else
                {
                    config.meshtastic_config.bandwidth_khz =
                        static_cast<float>(payload->value);
                    config.meshtastic_config.use_preset = false;
                    g_settings.net_use_preset = false;
                }
            });
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::NetSf)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                if (chat::infra::isReticulumMeshProtocol(config.mesh_protocol))
                {
                    config.reticulumConfig().spread_factor =
                        static_cast<uint8_t>(payload->value);
                }
                else
                {
                    config.meshtastic_config.spread_factor =
                        static_cast<uint8_t>(payload->value);
                    config.meshtastic_config.use_preset = false;
                    g_settings.net_use_preset = false;
                }
            });
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::NetCr)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                if (chat::infra::isReticulumMeshProtocol(config.mesh_protocol))
                {
                    config.reticulumConfig().coding_rate =
                        static_cast<uint8_t>(payload->value);
                }
                else
                {
                    config.meshtastic_config.coding_rate =
                        static_cast<uint8_t>(payload->value);
                    config.meshtastic_config.use_preset = false;
                    g_settings.net_use_preset = false;
                }
            });
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::NetHopLimit)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshtastic_config.hop_limit =
                    static_cast<uint8_t>(payload->value);
            });
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::NetChannelNum)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshtastic_config.channel_num =
                    static_cast<uint16_t>(payload->value);
            });
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::ScreenTimeout)
    {
        screen_runtime::set_timeout_ms(static_cast<uint32_t>(payload->value));
    }
    if (id == settings::ui::SettingId::ScreenBrightness)
    {
        const uint8_t brightness = static_cast<uint8_t>(clamp_screen_brightness(payload->value));
        g_settings.screen_brightness = brightness;
        device_runtime::set_screen_brightness(brightness);
    }
    if (id == settings::ui::SettingId::SpeakerVolume)
    {
        const uint8_t volume = static_cast<uint8_t>(payload->value);
        apply_message_tone_volume(volume);
        if (volume > 0)
        {
            // Immediate audible feedback so user can tune the level interactively.
            play_message_tone_preview();
        }
    }
    if (id == settings::ui::SettingId::GpsInterval)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        const uint32_t interval_ms =
            static_cast<uint32_t>(payload->value) * 1000u;
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [interval_ms](app::AppConfig& config)
            {
                config.gps_interval_ms = interval_ms;
            });
        gps_runtime::set_collection_interval(interval_ms);
    }
    if (id == settings::ui::SettingId::GpsInitBaud)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_init_baud = static_cast<uint32_t>(payload->value);
            });
        gps_runtime::set_receiver_init_config(make_gps_receiver_init_config(app_ctx.readConfig()));
    }
    if (id == settings::ui::SettingId::GpsInitProbeMs)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        int probe_ms = payload->value;
        if (probe_ms < kGpsInitProbeMinMs)
        {
            probe_ms = kGpsInitProbeMinMs;
        }
        if (probe_ms > kGpsInitProbeMaxMs)
        {
            probe_ms = kGpsInitProbeMaxMs;
        }
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [probe_ms](app::AppConfig& config)
            {
                config.gps_init_probe_ms = static_cast<uint32_t>(probe_ms);
            });
        gps_runtime::set_receiver_init_config(make_gps_receiver_init_config(app_ctx.readConfig()));
    }
    if (id == settings::ui::SettingId::GpsInitProfile)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_init_profile = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_receiver_init_config(make_gps_receiver_init_config(app_ctx.readConfig()));
    }
    if (id == settings::ui::SettingId::GpsInitRxm)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_init_rxm_policy = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_receiver_init_config(make_gps_receiver_init_config(app_ctx.readConfig()));
    }
    if (id == settings::ui::SettingId::GpsInitGnss)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_init_gnss_policy = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_receiver_init_config(make_gps_receiver_init_config(app_ctx.readConfig()));
    }
    if (id == settings::ui::SettingId::GpsInitNmea)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_init_nmea_policy = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_receiver_init_config(make_gps_receiver_init_config(app_ctx.readConfig()));
    }
    if (id == settings::ui::SettingId::GpsMode)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_mode = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_gnss_config(app_ctx.readConfig().gps_mode, app_ctx.readConfig().gps_sat_mask);
    }
    if (id == settings::ui::SettingId::GpsSatMask)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_sat_mask = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_gnss_config(app_ctx.readConfig().gps_mode, app_ctx.readConfig().gps_sat_mask);
    }
    if (id == settings::ui::SettingId::GpsStrategy)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_strategy = static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_power_strategy(static_cast<uint8_t>(payload->value));
    }
    if (id == settings::ui::SettingId::GpsAltRef)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_alt_ref = static_cast<uint8_t>(payload->value);
            });
    }
    if (id == settings::ui::SettingId::GpsCoordFmt)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.gps_coord_format = static_cast<uint8_t>(payload->value);
            });
    }
    if (id == settings::ui::SettingId::MapCoord)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::map(),
            [payload](app::AppConfig& config)
            {
                config.map_coord_system = static_cast<uint8_t>(payload->value);
            });
    }
    if (id == settings::ui::SettingId::MapSource)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        int source = payload->value;
        if (source < 0 || source > 2)
        {
            source = 0;
        }
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::map(),
            [source](app::AppConfig& config)
            {
                config.map_source = static_cast<uint8_t>(source);
            });
    }
    if (id == settings::ui::SettingId::MapTrackInterval)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::map(),
            [payload](app::AppConfig& config)
            {
                config.map_track_interval =
                    static_cast<uint8_t>(payload->value);
            });
        apply_track_interval_runtime(static_cast<uint32_t>(payload->value));
    }
    if (id == settings::ui::SettingId::MapTrackFormat)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::map(),
            [payload](app::AppConfig& config)
            {
                config.map_track_format = static_cast<uint8_t>(payload->value);
            });
        apply_track_format_runtime(static_cast<uint8_t>(payload->value));
    }
    if (id == settings::ui::SettingId::ChatChannel)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::chatUi(),
            [payload](app::AppConfig& config)
            {
                config.chat_channel = static_cast<uint8_t>(payload->value);
            });
        app_ctx.applyChatDefaults();
    }
    if (id == settings::ui::SettingId::NetUtil)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::network(),
            [payload](app::AppConfig& config)
            {
                config.net_channel_util = static_cast<uint8_t>(payload->value);
            });
        app_ctx.applyNetworkLimits();
    }
    if (id == settings::ui::SettingId::NetTxPower)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                if (chat::infra::isReticulumMeshProtocol(config.mesh_protocol))
                {
                    config.reticulumConfig().tx_power =
                        static_cast<int8_t>(payload->value);
                }
                else
                {
                    config.meshtastic_config.tx_power =
                        static_cast<int8_t>(payload->value);
                }
            });
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McRegionPreset)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        uint8_t preset_id = static_cast<uint8_t>(payload->value);
        if (!chat::meshcore::isValidRegionPresetId(preset_id))
        {
            preset_id = 0;
        }
        const auto* preset = preset_id > 0
                                 ? chat::meshcore::findRegionPresetById(preset_id)
                                 : nullptr;
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [preset_id, preset](app::AppConfig& config)
            {
                chat::MeshConfig& mc_cfg = config.meshcore_config;
                mc_cfg.meshcore_region_preset = preset_id;
                if (preset)
                {
                    mc_cfg.meshcore_freq_mhz = preset->freq_mhz;
                    mc_cfg.meshcore_bw_khz = preset->bw_khz;
                    mc_cfg.meshcore_sf = preset->sf;
                    mc_cfg.meshcore_cr = preset->cr;
                    mc_cfg.tx_power = preset->tx_power_dbm;
                }
            });
        g_settings.mc_region_preset = preset_id;
        if (preset)
        {
            float_to_text(preset->freq_mhz, g_settings.mc_freq, sizeof(g_settings.mc_freq), 3);
            float_to_text(preset->bw_khz, g_settings.mc_bw, sizeof(g_settings.mc_bw), 3);
            g_settings.mc_sf = preset->sf;
            g_settings.mc_cr = preset->cr;
            g_settings.mc_tx_power = preset->tx_power_dbm;
        }
        app_ctx.applyMeshConfig();
        rebuild_list = true;
    }
    if (id == settings::ui::SettingId::McSf)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshcore_config.meshcore_sf =
                    static_cast<uint8_t>(payload->value);
                config.meshcore_config.meshcore_region_preset = 0;
            });
        g_settings.mc_region_preset = 0;
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McCr)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshcore_config.meshcore_cr =
                    static_cast<uint8_t>(payload->value);
                config.meshcore_config.meshcore_region_preset = 0;
            });
        g_settings.mc_region_preset = 0;
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McTxPower)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshcore_config.tx_power =
                    static_cast<int8_t>(payload->value);
                config.meshcore_config.meshcore_region_preset = 0;
            });
        g_settings.mc_region_preset = 0;
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McFloodMax)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshcore_config.meshcore_flood_max =
                    static_cast<uint8_t>(payload->value);
            });
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McSendProfile)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshcore_config.meshcore_send_profile =
                    static_cast<chat::MeshCorePayloadSendProfile>(
                        payload->value);
            });
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McForwardProfile)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::mesh(),
            [payload](app::AppConfig& config)
            {
                config.meshcore_config.meshcore_forward_profile =
                    static_cast<chat::MeshCoreForwardProfile>(
                        payload->value);
            });
        app_ctx.applyMeshConfig();
    }
    if (id == settings::ui::SettingId::McChannelSlot)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::channels(),
            [payload](app::AppConfig& config)
            {
                chat::MeshConfig& mesh = config.meshcore_config;
                mesh.meshcore_channel_slot =
                    chat::normalizeMeshCoreChannelSlot(
                        static_cast<uint8_t>(payload->value));
                mesh.syncMeshCoreLegacyChannelMirror();
                ::settings::ui::channel::sync_meshcore_channel_fields(config,
                                                                      g_settings);
            });
        app_ctx.applyMeshConfig();
        refresh_visible_item_values();
    }
    if (id == settings::ui::SettingId::PrivacyEncrypt)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::privacy(),
            [payload](app::AppConfig& config)
            {
                config.privacy_encrypt_mode =
                    static_cast<uint8_t>(payload->value);
            });
        app_ctx.applyPrivacyConfig();
    }
    if (id == settings::ui::SettingId::ExternalNmea)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.external_nmea_output_hz =
                    static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_external_nmea_config(app_ctx.readConfig().external_nmea_output_hz,
                                              app_ctx.readConfig().external_nmea_sentence_mask);
    }
    if (id == settings::ui::SettingId::ExternalNmeaSent)
    {
        app::IAppFacade& app_ctx = app::appFacade();
        commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::gps(),
            [payload](app::AppConfig& config)
            {
                config.external_nmea_sentence_mask =
                    static_cast<uint8_t>(payload->value);
            });
        gps_runtime::set_external_nmea_config(app_ctx.readConfig().external_nmea_output_hz,
                                              app_ctx.readConfig().external_nmea_sentence_mask);
    }
    if (id == settings::ui::SettingId::TimezoneProfile)
    {
        const auto persistence_result =
            ::platform::ui::time::set_timezone_profile_id_and_persist(payload->value);
        g_settings.timezone_profile_id = ::platform::ui::time::timezone_profile_id();
        g_settings.timezone_offset_min = ::platform::ui::time::timezone_offset_min();
        if (persistence_result == ::platform::ui::time::TimezoneProfilePersistenceResult::Failed)
        {
            *payload->item->enum_value = g_settings.timezone_profile_id;
            update_item_value(*payload->widget);
            ::ui::feedback::show_notice(::ui::i18n::tr("Unable to save time zone"), 3000);
        }
        else
        {
            if (persistence_result == ::platform::ui::time::TimezoneProfilePersistenceResult::Deferred)
            {
                ::ui::feedback::show_notice(
                    ::ui::i18n::tr("Time zone saved locally; SD sync pending"), 3000);
            }
            rebuild_active_app = true;
        }
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
        ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Restarting..."),
                                       ::ui::i18n::tr("Applying setting"),
                                       85);
        ::ui::feedback::show_notice(::ui::i18n::tr("Restarting..."), 1500);
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
    ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Resetting..."),
                                   ::ui::i18n::tr("Clearing stored settings"),
                                   15);
    (void)perform_factory_reset();
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

    modal_add_focus_obj(cancel_btn);
    modal_add_focus_obj(confirm_btn);
    lv_group_focus_obj(cancel_btn);
}

static void format_gps_diagnostics_text(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const gps_runtime::GpsDiagnosticsSnapshot diag = gps_runtime::diagnostics();
    char last_rx[24];
    if (diag.last_rx_age_ms == 0xFFFFFFFFUL)
    {
        std::snprintf(last_rx, sizeof(last_rx), "never");
    }
    else
    {
        std::snprintf(last_rx, sizeof(last_rx), "%lu ms",
                      static_cast<unsigned long>(diag.last_rx_age_ms));
    }

    std::snprintf(out,
                  out_len,
                  "Code: %s\n"
                  "Supported: %d  Enabled: %d\n"
                  "Powered: %d  Ready: %d\n"
                  "Fix: %d  Sats: %u\n"
                  "View: %u  Use: %u\n"
                  "Chars: %lu  Recent: %lu\n"
                  "Last RX: %s\n"
                  "Poll: %lu ms  Publish: %lu ms",
                  ::gps::gpsDiagnosticCodeName(diag.code),
                  diag.supported ? 1 : 0,
                  diag.enabled ? 1 : 0,
                  diag.powered ? 1 : 0,
                  diag.ready ? 1 : 0,
                  diag.has_fix ? 1 : 0,
                  static_cast<unsigned>(diag.satellites),
                  static_cast<unsigned>(diag.sats_in_view),
                  static_cast<unsigned>(diag.sats_in_use),
                  static_cast<unsigned long>(diag.chars_total),
                  static_cast<unsigned long>(diag.chars_recent),
                  last_rx,
                  static_cast<unsigned long>(diag.poll_interval_ms),
                  static_cast<unsigned long>(diag.collection_interval_ms));

    std::printf("[GPS] diagnostics ui code=%s enabled=%d powered=%d ready=%d fix=%d sats=%u view=%u use=%u chars=%lu recent=%lu last_rx_age_ms=%lu\n",
                ::gps::gpsDiagnosticCodeName(diag.code),
                diag.enabled ? 1 : 0,
                diag.powered ? 1 : 0,
                diag.ready ? 1 : 0,
                diag.has_fix ? 1 : 0,
                static_cast<unsigned>(diag.satellites),
                static_cast<unsigned>(diag.sats_in_view),
                static_cast<unsigned>(diag.sats_in_use),
                static_cast<unsigned long>(diag.chars_total),
                static_cast<unsigned long>(diag.chars_recent),
                static_cast<unsigned long>(diag.last_rx_age_ms));
}

static void refresh_gps_diagnostics_label()
{
    if (!s_gps_diagnostics_label || !lv_obj_is_valid(s_gps_diagnostics_label))
    {
        return;
    }
    char text[360];
    format_gps_diagnostics_text(text, sizeof(text));
    ::ui::i18n::set_label_text_raw(s_gps_diagnostics_label, text);
}

static void on_gps_diagnostics_refresh_clicked(lv_event_t* e)
{
    (void)e;
    refresh_gps_diagnostics_label();
}

static void on_gps_diagnostics_close_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
}

static void open_gps_diagnostics_modal()
{
    if (g_state.modal_root)
    {
        return;
    }

    modal_prepare_group();
    g_state.modal_root = create_modal_root(300, 220);
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text(title, "GPS Diagnostics");
    style::apply_label_primary(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_gps_diagnostics_label = lv_label_create(win);
    lv_obj_set_width(s_gps_diagnostics_label, LV_PCT(100));
    lv_label_set_long_mode(s_gps_diagnostics_label, LV_LABEL_LONG_WRAP);
    style::apply_label_muted(s_gps_diagnostics_label);
    lv_obj_align(s_gps_diagnostics_label, LV_ALIGN_TOP_LEFT, 0, 28);
    refresh_gps_diagnostics_label();

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

    lv_obj_t* refresh_btn = lv_btn_create(btn_row);
    lv_obj_set_size(refresh_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    ::ui::i18n::set_label_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);
    lv_obj_add_event_cb(refresh_btn, on_gps_diagnostics_refresh_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_btn = lv_btn_create(btn_row);
    lv_obj_set_size(close_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* close_label = lv_label_create(close_btn);
    ::ui::i18n::set_label_text(close_label, "Close");
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn, on_gps_diagnostics_close_clicked, LV_EVENT_CLICKED, nullptr);

    modal_add_focus_obj(refresh_btn);
    modal_add_focus_obj(close_btn);
    lv_group_focus_obj(refresh_btn);
}

static const char* spi_diagnostics_card_type_label(uint8_t card_type)
{
    switch (card_type)
    {
    case 2:
        return "SD";
    case 3:
        return "SDHC";
    case 4:
        return "Unknown";
    default:
        return "None";
    }
}

static const char* spi_diagnostics_filesystem_label(uint8_t filesystem_type)
{
    switch (filesystem_type)
    {
    case 1:
        return "FAT12";
    case 2:
        return "FAT16";
    case 3:
        return "FAT32";
    case 4:
        return "exFAT";
    default:
        return "None";
    }
}

static const char* spi_diagnostics_health_label(uint8_t health_status)
{
    switch (health_status)
    {
    case 0:
        return "Healthy";
    case 1:
        return "Slow";
    case 2:
        return "Degraded";
    case 3:
        return "Unavailable";
    case 4:
        return "Recovering";
    default:
        return "Unknown";
    }
}

static const char* spi_diagnostics_policy_label(uint8_t policy)
{
    switch (policy)
    {
    case 0:
        return "UI";
    case 1:
        return "Display";
    case 2:
        return "Interactive";
    case 3:
        return "Background";
    case 4:
        return "Durable";
    case 5:
        return "Recovery";
    default:
        return "None";
    }
}

static void format_spi_diagnostics_init_rate(char* out, size_t out_len, uint32_t rate_hz)
{
    if (!out || out_len == 0U)
    {
        return;
    }

    if (rate_hz == 0U)
    {
        std::snprintf(out, out_len, "none");
        return;
    }

    std::snprintf(out,
                  out_len,
                  "%lu.%02lu MHz",
                  static_cast<unsigned long>(rate_hz / 1000000U),
                  static_cast<unsigned long>((rate_hz % 1000000U) / 10000U));
}

static void format_spi_diagnostics_text(char* out, size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }

#if defined(ARDUINO_ARCH_ESP32)
    // Static storage holds this compact scalar snapshot. No LVGL pixel buffer,
    // SD sector buffer, heap allocation, or large task-stack object is needed.
    static spi_diagnostics_runtime::Snapshot snapshot{};
    spi_diagnostics_runtime::read(snapshot);
    const uint32_t configured_mhz = snapshot.sd_configured_hz / 1000000U;
    const uint32_t configured_mhz_fraction =
        (snapshot.sd_configured_hz % 1000000U) / 10000U;
    format_spi_diagnostics_init_rate(s_spi_diagnostics_init_rate,
                                     sizeof(s_spi_diagnostics_init_rate),
                                     snapshot.sd_initialized_hz);
    std::snprintf(out,
                  out_len,
                  "SD: %s  Successful init: %s\n"
                  "Configured: %lu.%02lu MHz  Attempt: %u\n"
                  "Card: %s  FS: %s  Size: %lu MiB\n"
                  "SPI: %s (%s)  Health: %s (%ld)\n"
                  "Display: %lu req / %lu complete\n"
                  "Deferred: %lu  Busy: %lu  Fail: %lu\n"
                  "Wait max: %lu ms  Hold: %lu/%lu ms\n"
                  "Release mismatches: %lu",
                  snapshot.sd_ready ? "ready" : "not mounted",
                  s_spi_diagnostics_init_rate,
                  static_cast<unsigned long>(configured_mhz),
                  static_cast<unsigned long>(configured_mhz_fraction),
                  static_cast<unsigned>(snapshot.sd_initialization_attempts),
                  spi_diagnostics_card_type_label(snapshot.sd_card_type),
                  spi_diagnostics_filesystem_label(snapshot.sd_filesystem_type),
                  static_cast<unsigned long>(snapshot.sd_capacity_mib),
                  snapshot.bus_active ? "active" : "idle",
                  spi_diagnostics_policy_label(snapshot.bus_policy),
                  spi_diagnostics_health_label(snapshot.health_status),
                  static_cast<long>(snapshot.health_last_error),
                  static_cast<unsigned long>(snapshot.display_requests),
                  static_cast<unsigned long>(snapshot.display_completions),
                  static_cast<unsigned long>(snapshot.display_deferrals),
                  static_cast<unsigned long>(snapshot.display_busy_retries),
                  static_cast<unsigned long>(snapshot.display_failures),
                  static_cast<unsigned long>(snapshot.maximum_display_wait_ms),
                  static_cast<unsigned long>(snapshot.bus_held_ms),
                  static_cast<unsigned long>(snapshot.maximum_hold_ms),
                  static_cast<unsigned long>(snapshot.release_mismatches));
#else
    std::snprintf(out, out_len, "SPI diagnostics are unavailable on this target.");
#endif
}

static void refresh_spi_diagnostics_label()
{
    if (!s_spi_diagnostics_label || !lv_obj_is_valid(s_spi_diagnostics_label))
    {
        return;
    }
    format_spi_diagnostics_text(s_spi_diagnostics_text, sizeof(s_spi_diagnostics_text));
    ::ui::i18n::set_label_text_raw(s_spi_diagnostics_label, s_spi_diagnostics_text);
}

static void on_spi_diagnostics_refresh_clicked(lv_event_t* e)
{
    (void)e;
    refresh_spi_diagnostics_label();
}

static void on_spi_diagnostics_close_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
}

static void open_spi_diagnostics_modal()
{
    if (g_state.modal_root)
    {
        return;
    }

    modal_prepare_group();
    g_state.modal_root = create_modal_root(300, 240);
    lv_obj_t* win = lv_obj_get_child(g_state.modal_root, 0);

    lv_obj_t* title = lv_label_create(win);
    ::ui::i18n::set_label_text(title, "SPI & SD Diagnostics");
    style::apply_label_primary(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_spi_diagnostics_label = lv_label_create(win);
    lv_obj_set_width(s_spi_diagnostics_label, LV_PCT(100));
    lv_label_set_long_mode(s_spi_diagnostics_label, LV_LABEL_LONG_WRAP);
    style::apply_label_muted(s_spi_diagnostics_label);
    lv_obj_align(s_spi_diagnostics_label, LV_ALIGN_TOP_LEFT, 0, 28);
    refresh_spi_diagnostics_label();

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

    lv_obj_t* refresh_btn = lv_btn_create(btn_row);
    lv_obj_set_size(refresh_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    ::ui::i18n::set_label_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);
    lv_obj_add_event_cb(refresh_btn, on_spi_diagnostics_refresh_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_btn = lv_btn_create(btn_row);
    lv_obj_set_size(close_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    lv_obj_t* close_label = lv_label_create(close_btn);
    ::ui::i18n::set_label_text(close_label, "Close");
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn, on_spi_diagnostics_close_clicked, LV_EVENT_CLICKED, nullptr);

    modal_add_focus_obj(refresh_btn);
    modal_add_focus_obj(close_btn);
    lv_group_focus_obj(refresh_btn);
}

static void on_enabled_imes_back_clicked(lv_event_t* e)
{
    (void)e;
    modal_close();
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

static void on_ime_toggle_clicked(lv_event_t* e)
{
    ImeToggleClick* payload = static_cast<ImeToggleClick*>(lv_event_get_user_data(e));
    if (!payload || !payload->ime_id || !payload->widget)
    {
        return;
    }

    const bool currently_enabled = ::ui::i18n::ime_enabled(payload->ime_id);
    const bool next_enabled = !currently_enabled;
    if (!::ui::i18n::set_ime_enabled(payload->ime_id, next_enabled, true))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("IME setting update failed"), 3000);
        return;
    }

    lv_obj_t* btn = lv_event_get_target_obj(e);
    if (btn)
    {
        if (next_enabled)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
    }

    set_modal_toggle_state_label(payload->state_label, next_enabled);
    update_item_value(*payload->widget);
}

static void open_enabled_imes_modal(settings::ui::ItemWidget& widget)
{
    if (g_state.modal_root)
    {
        return;
    }

    refresh_language_pack_options();
    update_item_value(widget);

    const std::size_t ime_total = ::ui::i18n::ime_count();
    if (ime_total == 0)
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("No IME packs installed"), 3000);
        return;
    }

    modal_prepare_group();

    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t top_bar_h = profile.top_bar_height > 0
                                     ? profile.top_bar_height
                                     : static_cast<lv_coord_t>(::ui::widgets::kTopBarHeight);
    const lv_coord_t gap_from_top_bar = 3;
    const lv_coord_t content_h = lv_obj_get_height(g_state.root) - top_bar_h;

    g_state.modal_root = lv_obj_create(g_state.root);
    lv_obj_add_flag(g_state.modal_root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(g_state.modal_root, LV_PCT(100), content_h);
    lv_obj_set_pos(g_state.modal_root, 0, top_bar_h);
    style::apply_modal_bg(g_state.modal_root);
    style::apply_modal_panel(g_state.modal_root);
    lv_obj_set_style_border_width(g_state.modal_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.modal_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_state.modal_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(g_state.modal_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_state.modal_root, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* list = lv_obj_create(g_state.modal_root);
    lv_obj_set_size(list, LV_PCT(100), content_h - gap_from_top_bar);
    lv_obj_set_pos(list, 0, gap_from_top_bar);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    s_ime_toggle_count = 0;
    for (std::size_t index = 0; index < ime_total && s_ime_toggle_count < kMaxImeOptions; ++index)
    {
        const ::ui::i18n::ImeInfo* ime = ::ui::i18n::ime_at(index);
        if (!ime)
        {
            continue;
        }

        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), ::ui::page_profile::resolve_control_button_height());
        style::apply_btn_modal(btn);
        lv_obj_set_style_pad_left(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 12, LV_PART_MAIN);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn,
                              LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t* name_label = lv_label_create(btn);
        const char* display_name = ime->display_name ? ime->display_name : ime->id;
        if (::ui::fonts::utf8_has_non_ascii(display_name))
        {
            ::ui::i18n::set_content_label_text_raw(name_label, display_name);
        }
        else
        {
            ::ui::i18n::set_label_text_raw(name_label, display_name);
        }
        style::apply_label_primary(name_label);

        lv_obj_t* state_label = lv_label_create(btn);
        set_modal_toggle_state_label(state_label, ::ui::i18n::ime_enabled(ime->id));
        style::apply_label_primary(state_label);

        s_ime_toggle_clicks[s_ime_toggle_count].ime_id = ime->id;
        s_ime_toggle_clicks[s_ime_toggle_count].widget = &widget;
        s_ime_toggle_clicks[s_ime_toggle_count].state_label = state_label;
        lv_obj_add_event_cb(btn,
                            on_ime_toggle_clicked,
                            LV_EVENT_CLICKED,
                            &s_ime_toggle_clicks[s_ime_toggle_count]);
        lv_obj_add_event_cb(btn, option_modal_focused_cb, LV_EVENT_FOCUSED, nullptr);
        if (::ui::i18n::ime_enabled(ime->id))
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        modal_add_focus_obj(btn);
        ++s_ime_toggle_count;
    }

    lv_obj_t* back_btn = lv_btn_create(list);
    lv_obj_set_size(back_btn, LV_PCT(100), ::ui::page_profile::resolve_control_button_height());
    style::apply_btn_modal(back_btn);
    lv_obj_t* back_label = lv_label_create(back_btn);
    ::ui::i18n::set_label_text(back_label, "Back");
    style::apply_label_primary(back_label);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, on_enabled_imes_back_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(back_btn, option_modal_focused_cb, LV_EVENT_FOCUSED, nullptr);
    modal_add_focus_obj(back_btn);

    if (s_ime_toggle_count > 0)
    {
        lv_group_focus_obj(lv_obj_get_child(list, 0));
    }
    else
    {
        lv_group_focus_obj(back_btn);
    }
}

static void open_option_modal(const settings::ui::SettingItem& item, settings::ui::ItemWidget& widget)
{
    if (g_state.modal_root)
    {
        return;
    }
    if (item_id(item) == settings::ui::SettingId::DisplayLocale)
    {
        refresh_language_pack_options();
        update_item_value(widget);
    }
    modal_prepare_group();

    // Overlay only below top bar; list starts 3px under top bar, no title
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t kTopBarH = profile.top_bar_height > 0 ? profile.top_bar_height
                                                           : static_cast<lv_coord_t>(::ui::widgets::kTopBarHeight);
    const lv_coord_t kGapFromTopBar = 3;

    lv_coord_t content_h = lv_obj_get_height(g_state.root) - kTopBarH;
    g_state.modal_root = lv_obj_create(g_state.root);
    lv_obj_add_flag(g_state.modal_root, LV_OBJ_FLAG_IGNORE_LAYOUT);
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
        apply_locale_preview_font(label, item, item.options[i].value);

        s_option_clicks[s_option_click_count] = {&item, item.options[i].value, &widget};
        lv_obj_add_event_cb(btn, on_option_clicked, LV_EVENT_CLICKED,
                            &s_option_clicks[s_option_click_count]);
        if (item.enum_value && item.options[i].value == *item.enum_value)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(btn, option_modal_focused_cb, LV_EVENT_FOCUSED, nullptr);
        modal_add_focus_obj(btn);
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
    modal_add_focus_obj(back_btn);

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
static const settings::ui::SettingOption kGpsInitBaudOptions[] = {
    {"Auto", 0},
    {"9600", 9600},
    {"38400", 38400},
    {"115200", 115200},
    {"57600", 57600},
    {"19200", 19200},
    {"4800", 4800},
};
static const settings::ui::SettingOption kGpsInitProbeOptions[] = {
    {"250 ms", 250},
    {"500 ms", 500},
    {"900 ms", 900},
    {"1600 ms", 1600},
};
static const settings::ui::SettingOption kGpsInitProfileOptions[] = {
    {"Auto", 0},
    {"NMEA Passive", 1},
    {"u-blox Legacy", 2},
    {"u-blox Modern", 3},
};
static const settings::ui::SettingOption kGpsInitPolicyOptions[] = {
    {"Auto", 0},
    {"Skip", 1},
    {"Send", 2},
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
    {"Reticulum", static_cast<int>(chat::MeshProtocol::Reticulum)},
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
static const settings::ui::SettingOption kReticulumBearerOptions[] = {
    {"Auto", static_cast<int>(chat::ReticulumInterfacePolicy::All)},
    {"LoRa", static_cast<int>(chat::ReticulumInterfacePolicy::LoRaOnly)},
    {"Wi-Fi", static_cast<int>(chat::ReticulumInterfacePolicy::WifiGatewayOnly)},
};
static const settings::ui::SettingOption kReticulumTcpSlotOptions[] = {
    {"1", 0}, {"2", 1}, {"3", 2}};
static const settings::ui::SettingOption kChatContactAlertOptions[] = {
    {"OFF", kChatContactAlertsNone},
    {"Contacts Only", kChatContactAlertsContacts},
    {"All", kChatContactAlertsAll},
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
    {"0 Public", 0},
    {"1", 1},
    {"2", 2},
    {"3", 3},
    {"4", 4},
    {"5", 5},
    {"6", 6},
    {"7", 7},
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
static const settings::ui::SettingOption kMeshCoreSendProfileOptions[] = {
    {"V1 Only", 0},
    {"Auto Prefer V2", 1},
    {"V2 Only", 2},
};
static const settings::ui::SettingOption kMeshCoreForwardProfileOptions[] = {
    {"Any", 0},
    {"Multibyte Only", 1},
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
static const settings::ui::SettingOption kExternalNmeaOptions[] = {
    {"OFF", 0},
    {"1Hz", 1},
    {"5Hz", 5},
};
static const settings::ui::SettingOption kExternalNmeaSentenceOptions[] = {
    {"GGA+RMC+GSA+GSV", 0},
    {"RMC+GSA+GSV", 1},
    {"GGA+RMC", 2},
};

static const settings::ui::SettingOption kScreenTimeoutOptions[] = {
    {"15s", 15000},
    {"30s", 30000},
    {"1min", 60000},
    {"Always", 300000},
};

static const settings::ui::SettingOption kScreenBrightnessOptions[] = {
    {platform::ui::screen_brightness_steps::kStepLabels[0], platform::ui::screen_brightness_steps::levelForStep(0, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[1], platform::ui::screen_brightness_steps::levelForStep(1, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[2], platform::ui::screen_brightness_steps::levelForStep(2, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[3], platform::ui::screen_brightness_steps::levelForStep(3, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[4], platform::ui::screen_brightness_steps::levelForStep(4, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[5], platform::ui::screen_brightness_steps::levelForStep(5, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[6], platform::ui::screen_brightness_steps::levelForStep(6, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[7], platform::ui::screen_brightness_steps::levelForStep(7, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[8], platform::ui::screen_brightness_steps::levelForStep(8, kScreenBrightnessMax)},
    {platform::ui::screen_brightness_steps::kStepLabels[9], platform::ui::screen_brightness_steps::levelForStep(9, kScreenBrightnessMax)},
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

static settings::ui::SettingItem kProfileItems[] = {
    {"User Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.user_name, sizeof(g_settings.user_name), false, "chat_user"},
    {"Short Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.short_name, sizeof(g_settings.short_name), false, "chat_short"},
    {"Protocol", settings::ui::SettingType::Enum, kChatProtocolOptions, 3, &g_settings.chat_protocol, nullptr, nullptr, 0, false, "mesh_protocol"},
    {"Message Alerts", settings::ui::SettingType::Enum, kBoolOptions, sizeof(kBoolOptions) / sizeof(kBoolOptions[0]), &g_settings.chat_message_alerts, nullptr, nullptr, 0, false, "chat_message_alerts"},
    {"Contact Alerts", settings::ui::SettingType::Enum, kChatContactAlertOptions, sizeof(kChatContactAlertOptions) / sizeof(kChatContactAlertOptions[0]), &g_settings.chat_contact_alerts, nullptr, nullptr, 0, false, "chat_contact_alerts"},
    {"Auto Reply", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.chat_auto_reply_enabled, nullptr, 0, false, "chat_auto_reply_enabled"},
    {"Auto Reply Text", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.chat_auto_reply_text, sizeof(g_settings.chat_auto_reply_text), false, "chat_auto_reply_text"},
};

static settings::ui::SettingItem kMeshItems[] = {
    {"Region", settings::ui::SettingType::Enum, nullptr, 0, &g_settings.chat_region, nullptr, nullptr, 0, false, "chat_region"},
    {"Active Chat Channel", settings::ui::SettingType::Enum, kChatChannelOptions, 2, &g_settings.chat_channel, nullptr, nullptr, 0, false, "chat_channel"},
    {"MT Primary Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_primary_enabled, nullptr, 0, false, "mt_primary_enabled"},
    {"MT Primary Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_primary_name, sizeof(g_settings.mt_primary_name), false, "mt_primary_name"},
    {"MT Primary PSK", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_primary_key, sizeof(g_settings.mt_primary_key), true, "mt_primary_key"},
    {"Generate MT Primary PSK", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "mt_primary_key_generate"},
    {"MT Primary Uplink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_primary_uplink, nullptr, 0, false, "mt_primary_uplink"},
    {"MT Primary Downlink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_primary_downlink, nullptr, 0, false, "mt_primary_downlink"},
    {"MT Secondary Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_secondary_enabled, nullptr, 0, false, "mt_secondary_enabled"},
    {"MT Secondary Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_secondary_name, sizeof(g_settings.mt_secondary_name), false, "mt_secondary_name"},
    {"MT Secondary PSK", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_secondary_key, sizeof(g_settings.mt_secondary_key), true, "mt_secondary_key"},
    {"Generate MT Secondary PSK", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "mt_secondary_key_generate"},
    {"MT Secondary Uplink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_secondary_uplink, nullptr, 0, false, "mt_secondary_uplink"},
    {"MT Secondary Downlink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_secondary_downlink, nullptr, 0, false, "mt_secondary_downlink"},
    {"Encryption Mode", settings::ui::SettingType::Enum, kPrivacyEncryptOptions, 3, &g_settings.privacy_encrypt_mode, nullptr, nullptr, 0, false, "privacy_encrypt"},
    {"MT MQTT", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_mqtt_enabled, nullptr, 0, false, "mt_mqtt_enabled"},
    {"MT MQTT Host", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_mqtt_host, sizeof(g_settings.mt_mqtt_host), false, "mt_mqtt_host"},
    {"MT MQTT Port", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_mqtt_port, sizeof(g_settings.mt_mqtt_port), false, "mt_mqtt_port"},
    {"MT MQTT Root", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_mqtt_root, sizeof(g_settings.mt_mqtt_root), false, "mt_mqtt_root"},
    {"MT MQTT User", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_mqtt_user, sizeof(g_settings.mt_mqtt_user), false, "mt_mqtt_user"},
    {"MT MQTT Pass", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mt_mqtt_pass, sizeof(g_settings.mt_mqtt_pass), true, "mt_mqtt_pass"},
    {"MT MQTT Uplink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_mqtt_uplink, nullptr, 0, false, "mt_mqtt_uplink"},
    {"MT MQTT Downlink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mt_mqtt_downlink, nullptr, 0, false, "mt_mqtt_downlink"},
    {"MC MQTT", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_mqtt_enabled, nullptr, 0, false, "mc_mqtt_enabled"},
    {"MC MQTT Host", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_mqtt_host, sizeof(g_settings.mc_mqtt_host), false, "mc_mqtt_host"},
    {"MC MQTT Port", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_mqtt_port, sizeof(g_settings.mc_mqtt_port), false, "mc_mqtt_port"},
    {"MC MQTT Root", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_mqtt_root, sizeof(g_settings.mc_mqtt_root), false, "mc_mqtt_root"},
    {"MC MQTT User", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_mqtt_user, sizeof(g_settings.mc_mqtt_user), false, "mc_mqtt_user"},
    {"MC MQTT Pass", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_mqtt_pass, sizeof(g_settings.mc_mqtt_pass), true, "mc_mqtt_pass"},
    {"MC MQTT Uplink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_mqtt_uplink, nullptr, 0, false, "mc_mqtt_uplink"},
    {"MC MQTT Downlink", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_mqtt_downlink, nullptr, 0, false, "mc_mqtt_downlink"},
    {"MC Channel Slot", settings::ui::SettingType::Enum, kMeshCoreChannelSlotOptions, sizeof(kMeshCoreChannelSlotOptions) / sizeof(kMeshCoreChannelSlotOptions[0]), &g_settings.mc_channel_slot, nullptr, nullptr, 0, false, "mc_channel_slot"},
    {"MC Channel Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_channel_enabled, nullptr, 0, false, "mc_channel_enabled"},
    {"MC Channel Name", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_channel_name, sizeof(g_settings.mc_channel_name), false, "mc_channel_name"},
    {"MC Channel Key", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_channel_key, sizeof(g_settings.mc_channel_key), true, "mc_channel_key"},
    {"Generate MC Channel Key", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "mc_channel_key_generate"},
    {"Clear MC Channel", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "mc_channel_clear"},
    {"Bearer", settings::ui::SettingType::Enum, kReticulumBearerOptions, sizeof(kReticulumBearerOptions) / sizeof(kReticulumBearerOptions[0]), &g_settings.rt_bearer_policy, nullptr, nullptr, 0, false, "rt_bearer"},
    {"Display Name", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.rt_display_name, sizeof(g_settings.rt_display_name), false, "rt_display_name"},
    {"Identity Hash", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.rt_identity_hash, sizeof(g_settings.rt_identity_hash), false, "rt_identity_hash"},
    {"LXMF Address", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.rt_lxmf_address, sizeof(g_settings.rt_lxmf_address), false, "rt_lxmf_address"},
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
    {"TCP Entry", settings::ui::SettingType::Enum, kReticulumTcpSlotOptions, 3, &g_settings.rt_tcp_slot, nullptr, nullptr, 0, false, "rt_tcp_slot"},
    {"Restore Reticulum TCP defaults", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "rt_tcp_defaults"},
#endif
    {"Gateway Host", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.rt_wifi_gateway_host, sizeof(g_settings.rt_wifi_gateway_host), false, "rt_wifi_host"},
    {"Gateway Port", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.rt_wifi_gateway_port, sizeof(g_settings.rt_wifi_gateway_port), false, "rt_wifi_port"},
    {"Auto Wi-Fi", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.rt_wifi_auto_connect, nullptr, 0, false, "rt_wifi_auto"},
    {"Anonymous Peer", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.rt_anonymous_peer, nullptr, 0, false, "rt_anonymous_peer"},
    {"Location Requests", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.rt_location_requests, nullptr, 0, false, "rt_location_requests"},
};

static settings::ui::SettingItem kRadioItems[] = {
    {"Use Preset", settings::ui::SettingType::Enum, kBoolOptions, sizeof(kBoolOptions) / sizeof(kBoolOptions[0]), &g_settings.net_use_preset, nullptr, nullptr, 0, false, "net_use_preset"},
    {"Modem Preset", settings::ui::SettingType::Enum, kNetPresetOptions, sizeof(kNetPresetOptions) / sizeof(kNetPresetOptions[0]), &g_settings.net_modem_preset, nullptr, nullptr, 0, false, "net_preset"},
    {"Manual BW", settings::ui::SettingType::Enum, kNetManualBwOptions, sizeof(kNetManualBwOptions) / sizeof(kNetManualBwOptions[0]), &g_settings.net_manual_bw, nullptr, nullptr, 0, false, "net_bw"},
    {"Manual SF", settings::ui::SettingType::Enum, kSfOptions, sizeof(kSfOptions) / sizeof(kSfOptions[0]), &g_settings.net_manual_sf, nullptr, nullptr, 0, false, "net_sf"},
    {"Manual CR", settings::ui::SettingType::Enum, kCrOptions, sizeof(kCrOptions) / sizeof(kCrOptions[0]), &g_settings.net_manual_cr, nullptr, nullptr, 0, false, "net_cr"},
    {"TX Power", settings::ui::SettingType::Enum, nullptr,
     0, &g_settings.net_tx_power, nullptr, nullptr, 0, false, "net_tx_power"},
    {"Hop Limit", settings::ui::SettingType::Enum, kHopLimitOptions, sizeof(kHopLimitOptions) / sizeof(kHopLimitOptions[0]), &g_settings.net_hop_limit, nullptr, nullptr, 0, false, "net_hop_limit"},
    {"TX Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_tx_enabled, nullptr, 0, false, "net_tx_enabled"},
    {"Override Duty Cycle", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_override_duty_cycle, nullptr, 0, false, "net_override_duty"},
    {"Channel Slot", settings::ui::SettingType::Enum, kChannelNumOptions, sizeof(kChannelNumOptions) / sizeof(kChannelNumOptions[0]), &g_settings.net_channel_num, nullptr, nullptr, 0, false, "net_channel_num"},
    {"Freq Offset (MHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.net_freq_offset, sizeof(g_settings.net_freq_offset), false, "net_freq_offset"},
    {"Override Freq (MHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.net_override_freq, sizeof(g_settings.net_override_freq), false, "net_override_freq"},
    {"MC Region Preset", settings::ui::SettingType::Enum, nullptr, 0, &g_settings.mc_region_preset, nullptr, nullptr, 0, false, "mc_region_preset"},
    {"MC Frequency (MHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_freq, sizeof(g_settings.mc_freq), false, "mc_freq"},
    {"MC Bandwidth (kHz)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_bw, sizeof(g_settings.mc_bw), false, "mc_bw"},
    {"MC Spread Factor", settings::ui::SettingType::Enum, kSfOptions, sizeof(kSfOptions) / sizeof(kSfOptions[0]), &g_settings.mc_sf, nullptr, nullptr, 0, false, "mc_sf"},
    {"MC Coding Rate", settings::ui::SettingType::Enum, kCrOptions, sizeof(kCrOptions) / sizeof(kCrOptions[0]), &g_settings.mc_cr, nullptr, nullptr, 0, false, "mc_cr"},
    {"MC TX Power", settings::ui::SettingType::Enum, nullptr, 0, &g_settings.mc_tx_power, nullptr, nullptr, 0, false, "mc_tx_power"},
    {"MC Repeat", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_client_repeat, nullptr, 0, false, "mc_repeat"},
    {"MC RX Delay Base", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_rx_delay, sizeof(g_settings.mc_rx_delay), false, "mc_rx_delay"},
    {"MC Airtime Factor", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.mc_airtime, sizeof(g_settings.mc_airtime), false, "mc_airtime"},
    {"MC Flood Max", settings::ui::SettingType::Enum, kMeshCoreFloodOptions, sizeof(kMeshCoreFloodOptions) / sizeof(kMeshCoreFloodOptions[0]), &g_settings.mc_flood_max, nullptr, nullptr, 0, false, "mc_flood_max"},
    {"MC Multi ACKs", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.mc_multi_acks, nullptr, 0, false, "mc_multi_acks"},
    {"MC Send Profile", settings::ui::SettingType::Enum, kMeshCoreSendProfileOptions, sizeof(kMeshCoreSendProfileOptions) / sizeof(kMeshCoreSendProfileOptions[0]), &g_settings.mc_send_profile, nullptr, nullptr, 0, false, "mc_send_prof"},
    {"MC Forward Profile", settings::ui::SettingType::Enum, kMeshCoreForwardProfileOptions, sizeof(kMeshCoreForwardProfileOptions) / sizeof(kMeshCoreForwardProfileOptions[0]), &g_settings.mc_forward_profile, nullptr, nullptr, 0, false, "mc_fwd_prof"},
    {"Duty Cycle Limit", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.net_duty_cycle, nullptr, 0, false, "net_duty_cycle"},
    {"Channel Utilization", settings::ui::SettingType::Enum, kNetUtilOptions, 3, &g_settings.net_channel_util, nullptr, nullptr, 0, false, "net_util"},
};

static settings::ui::SettingItem kLocationItems[] = {
    {"GPS Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.gps_enabled, nullptr, 0, false, "gps_enabled"},
    {"Receiver Baud", settings::ui::SettingType::Enum, kGpsInitBaudOptions, 7, &g_settings.gps_init_baud, nullptr, nullptr, 0, false, "gps_init_baud"},
    {"Probe Window", settings::ui::SettingType::Enum, kGpsInitProbeOptions, 4, &g_settings.gps_init_probe_ms, nullptr, nullptr, 0, false, "gps_init_probe_ms"},
    {"Receiver Profile", settings::ui::SettingType::Enum, kGpsInitProfileOptions, 4, &g_settings.gps_init_profile, nullptr, nullptr, 0, false, "gps_init_profile"},
    {"RXM Init", settings::ui::SettingType::Enum, kGpsInitPolicyOptions, 3, &g_settings.gps_init_rxm_policy, nullptr, nullptr, 0, false, "gps_init_rxm"},
    {"GNSS Init", settings::ui::SettingType::Enum, kGpsInitPolicyOptions, 3, &g_settings.gps_init_gnss_policy, nullptr, nullptr, 0, false, "gps_init_gnss"},
    {"NMEA Init", settings::ui::SettingType::Enum, kGpsInitPolicyOptions, 3, &g_settings.gps_init_nmea_policy, nullptr, nullptr, 0, false, "gps_init_nmea"},
    {"Location Mode", settings::ui::SettingType::Enum, kGpsModeOptions, 3, &g_settings.gps_mode, nullptr, nullptr, 0, false, "gps_mode"},
    {"Satellite Systems", settings::ui::SettingType::Enum, kGpsSatOptions, 5, &g_settings.gps_sat_mask, nullptr, nullptr, 0, false, "gps_sat_mask"},
    {"Position Strategy", settings::ui::SettingType::Enum, kGpsStrategyOptions, 3, &g_settings.gps_strategy, nullptr, nullptr, 0, false, "gps_strategy"},
    {"Update Interval", settings::ui::SettingType::Enum, kGpsIntervalOptions, 4, &g_settings.gps_interval, nullptr, nullptr, 0, false, "gps_interval"},
    {"Altitude Reference", settings::ui::SettingType::Enum, kGpsAltOptions, 2, &g_settings.gps_alt_ref, nullptr, nullptr, 0, false, "gps_alt_ref"},
    {"Coordinate Format", settings::ui::SettingType::Enum, kGpsCoordOptions, 3, &g_settings.gps_coord_format, nullptr, nullptr, 0, false, "gps_coord_fmt"},
    {"NMEA Export", settings::ui::SettingType::Enum, kExternalNmeaOptions, 3, &g_settings.external_nmea_output_hz, nullptr, nullptr, 0, false, "external_nmea"},
    {"NMEA Sentences", settings::ui::SettingType::Enum, kExternalNmeaSentenceOptions, 3, &g_settings.external_nmea_sentence_mask, nullptr, nullptr, 0, false, "external_nmea_sent"},
    {"Diagnostics", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "gps_diagnostics"},
    {"Coordinate System", settings::ui::SettingType::Enum, kMapCoordOptions, 3, &g_settings.map_coord_system, nullptr, nullptr, 0, false, "map_coord"},
    {"Map Source", settings::ui::SettingType::Enum, kMapSourceOptions, 3, &g_settings.map_source, nullptr, nullptr, 0, false, "map_source"},
    {"Contour Overlay", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.map_contour_enabled, nullptr, 0, false, "map_contour"},
    {"Track Recording", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.map_track_enabled, nullptr, 0, false, "map_track"},
    {"Track Interval", settings::ui::SettingType::Enum, kMapTrackIntervalOptions, 4, &g_settings.map_track_interval, nullptr, nullptr, 0, false, "map_track_interval"},
    {"Track Format", settings::ui::SettingType::Enum, kMapTrackFormatOptions, 3, &g_settings.map_track_format, nullptr, nullptr, 0, false, "map_track_format"},
};

static settings::ui::SettingItem kDeviceItems[] = {
    {"Display Language", settings::ui::SettingType::Enum, nullptr,
     0, &g_settings.display_locale_index, nullptr, nullptr, 0, false,
     "display_locale"},
    {"Enabled IMEs", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "enabled_imes"},
    {"Screen Timeout", settings::ui::SettingType::Enum, kScreenTimeoutOptions, 4, &g_settings.screen_timeout_ms, nullptr, nullptr, 0, false, "screen_timeout"},
    {"Screen Brightness", settings::ui::SettingType::Enum, kScreenBrightnessOptions,
     sizeof(kScreenBrightnessOptions) / sizeof(kScreenBrightnessOptions[0]), &g_settings.screen_brightness, nullptr, nullptr, 0, false, "screen_brightness"},
    {"Speaker Volume", settings::ui::SettingType::Enum, kSpeakerVolumeOptions,
     sizeof(kSpeakerVolumeOptions) / sizeof(kSpeakerVolumeOptions[0]), &g_settings.speaker_volume, nullptr, nullptr, 0, false, "speaker_volume"},
    {"Vibration", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.vibration_enabled, nullptr, 0, false, "vibration_enabled"},
    {"C6 Companion", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr,
     g_settings.c6_companion_status, sizeof(g_settings.c6_companion_status), false, "c6_companion_status"},
    {"C6 Download Mode", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "c6_enter_download"},
    {"Time Zone", settings::ui::SettingType::Enum, nullptr, 0, &g_settings.timezone_profile_id, nullptr, nullptr, 0, false, "timezone_profile"},
    {"Date/Time", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "manual_time_set"},
    {"Gauge Design (mAh)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr,
     g_settings.gauge_design_mah, sizeof(g_settings.gauge_design_mah), false, "gauge_design_mah"},
    {"Gauge Full (mAh)", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr,
     g_settings.gauge_full_mah, sizeof(g_settings.gauge_full_mah), false, "gauge_full_mah"},
    {"SPI & SD Diagnostics", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr,
     nullptr, 0, false, "spi_diagnostics"},
};

static settings::ui::SettingItem kWifiItems[] = {
    {"Wi-Fi Enabled", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.wifi_enabled, nullptr, 0, false, "wifi_enabled"},
    {"Status", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.wifi_status, sizeof(g_settings.wifi_status), false, "wifi_status"},
    {"Scan Networks", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "wifi_scan"},
    {"Detected Network", settings::ui::SettingType::Enum, nullptr, 0, &g_settings.wifi_network_index, nullptr, nullptr, 0, false, "wifi_network"},
    {"SSID", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), false, "wifi_ssid"},
    {"Password", settings::ui::SettingType::Text, nullptr, 0, nullptr, nullptr, g_settings.wifi_password, sizeof(g_settings.wifi_password), true, "wifi_password"},
    {"Connect", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "wifi_connect"},
    {"Disconnect", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "wifi_disconnect"},
};

static settings::ui::SettingItem kMaintenanceItems[] = {
    {"Current Version", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.fw_current_version, sizeof(g_settings.fw_current_version), false, "fw_current"},
    {"Latest Version", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.fw_latest_version, sizeof(g_settings.fw_latest_version), false, "fw_latest"},
    {"OTA Status", settings::ui::SettingType::Info, nullptr, 0, nullptr, nullptr, g_settings.fw_update_status, sizeof(g_settings.fw_update_status), false, "fw_status"},
    {"Check for Updates", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "fw_check"},
    {"Install Update", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "fw_install"},
    {"Debug Logs", settings::ui::SettingType::Toggle, nullptr, 0, nullptr, &g_settings.advanced_debug_logs, nullptr, 0, false, "adv_debug"},
    {"Reset Mesh Profiles", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_reset_mesh"},
    {"Reset Node DB", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_reset_nodes"},
    {"Clear Message DB", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "chat_clear_messages"},
    {"Factory Reset", settings::ui::SettingType::Action, nullptr, 0, nullptr, nullptr, nullptr, 0, false, "system_factory_reset"},
};

static const CategoryDef kCategories[] = {
    {"Profile", kProfileItems, sizeof(kProfileItems) / sizeof(kProfileItems[0])},
    {"Mesh", kMeshItems, sizeof(kMeshItems) / sizeof(kMeshItems[0])},
    {"Radio", kRadioItems, sizeof(kRadioItems) / sizeof(kRadioItems[0])},
    {"Wi-Fi", kWifiItems, sizeof(kWifiItems) / sizeof(kWifiItems[0])},
    {"Location", kLocationItems, sizeof(kLocationItems) / sizeof(kLocationItems[0])},
    {"Device", kDeviceItems, sizeof(kDeviceItems) / sizeof(kDeviceItems[0])},
    {"Maintenance", kMaintenanceItems, sizeof(kMaintenanceItems) / sizeof(kMaintenanceItems[0])},
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

static settings::ui::SettingId item_id(const settings::ui::SettingItem& item)
{
    return item.id != settings::ui::SettingId::Unknown
               ? item.id
               : ::settings::ui::spec::id_for_key(item.pref_key);
}

static bool option_labels_are_translated(const settings::ui::SettingItem& item)
{
    return ::settings::ui::spec::option_labels_are_translated(item_id(item));
}

static bool option_labels_use_content_font(const settings::ui::SettingItem& item)
{
    return ::settings::ui::spec::option_labels_use_content_font(item_id(item));
}

static const ::ui::i18n::LocaleInfo* locale_info_for_option_value(const settings::ui::SettingItem& item, int value)
{
    if (item_id(item) != settings::ui::SettingId::DisplayLocale || value < 0)
    {
        return nullptr;
    }

    return ::ui::i18n::locale_at(static_cast<std::size_t>(value));
}

static void apply_locale_preview_font(lv_obj_t* label, const settings::ui::SettingItem& item, int value)
{
    if (!label)
    {
        return;
    }

    const ::ui::i18n::LocaleInfo* locale = locale_info_for_option_value(item, value);
    if (!locale || !locale->id)
    {
        return;
    }

    const lv_font_t* preview_font =
        ::ui::i18n::locale_preview_font(locale->id, ::ui::fonts::ui_chrome_font());
    if (preview_font)
    {
        lv_obj_set_style_text_font(label, preview_font, 0);
    }
}

static bool should_show_item(const settings::ui::SettingItem& item)
{
    ::settings::ui::spec::VisibilityContext context{};
    context.protocol = selected_protocol();
    context.wifi_supported = wifi_runtime::is_supported();
    context.has_wifi_networks = kWifiNetworkOptionCount > 0;
    context.firmware_update_supported = firmware_update_runtime::is_supported();
    context.wireless_companion_supported = wireless_companion_runtime::is_supported();
    context.mt_secondary_enabled = g_settings.mt_secondary_enabled;
    context.mt_use_preset = g_settings.net_use_preset != 0;
    context.reticulum_wifi_visible = reticulum_wifi_settings_visible();
    context.reticulum_lora_visible = reticulum_lora_settings_visible();
    context.screen_brightness_supported = device_runtime::supports_screen_brightness();
    context.screen_timeout_supported = screen_runtime::supports_app_timeout_setting();
    context.gps_baud_supported = gps_runtime::supports_receiver_baud_setting();
    context.gps_init_policy_supported = gps_runtime::supports_receiver_init_policy_settings();
    context.gps_gnss_supported = gps_runtime::supports_gnss_runtime_settings();
    context.gps_interval_supported = gps_runtime::supports_collection_interval_setting();
    context.gps_alt_ref_supported = gps_runtime::supports_altitude_reference_setting();
    context.gps_coord_format_supported = gps_runtime::supports_coordinate_format_setting();
    context.external_nmea_supported = gps_runtime::supports_external_nmea_output_setting();
    context.battery_gauge_supported = device_runtime::supports_configurable_battery_gauge();
    return ::settings::ui::spec::should_show(item_id(item), context);
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

static lv_obj_t* create_staged_list_content(lv_obj_t* parent)
{
    if (parent == nullptr)
    {
        return nullptr;
    }

    lv_obj_t* content = lv_obj_create(parent);
    if (content == nullptr)
    {
        return nullptr;
    }
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content,
                             ::ui::page_profile::current().list_panel_pad_row,
                             LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    return content;
}

static bool visible_item_layout_matches_current()
{
    if (g_state.visible_list_content == nullptr ||
        g_state.current_category < 0 ||
        g_state.current_category >= static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0])))
    {
        return false;
    }

    const CategoryDef& category = kCategories[g_state.current_category];
    std::size_t expected_index = 0;
    for (std::size_t index = 0; index < category.item_count; ++index)
    {
        if (!should_show_item(category.items[index]))
        {
            continue;
        }
        if (expected_index >= g_state.item_count ||
            g_state.item_widgets[expected_index].def != &category.items[index])
        {
            return false;
        }
        ++expected_index;
    }
    return expected_index == g_state.item_count;
}

static bool begin_list_transaction()
{
    if (g_state.list_panel == nullptr)
    {
        return false;
    }

    if (g_state.list_content != nullptr &&
        g_state.list_content != g_state.visible_list_content)
    {
        // A nested refresh supersedes a not-yet-visible staging tree. The
        // previous visible tree remains untouched until a complete successor
        // has been built.
        lv_obj_del(g_state.list_content);
    }

    g_state.list_content = create_staged_list_content(g_state.list_panel);
    if (g_state.list_content == nullptr)
    {
        return false;
    }
    lv_obj_add_flag(g_state.list_content, LV_OBJ_FLAG_HIDDEN);
    return true;
}

static void commit_list_transaction()
{
    if (g_state.list_content == nullptr ||
        g_state.list_content == g_state.visible_list_content)
    {
        return;
    }

    lv_obj_clear_flag(g_state.list_content, LV_OBJ_FLAG_HIDDEN);
    if (g_state.visible_list_content != nullptr)
    {
        lv_obj_del(g_state.visible_list_content);
    }
    g_state.visible_list_content = g_state.list_content;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag,
             "[UI][Txn] settings kind=structure visible_clean=0 staging_commit=1 items=%u",
             static_cast<unsigned>(g_state.item_count));
#endif
}

static void build_item_list()
{
    if (!g_state.list_panel) return;
    if (s_building_list)
    {
        return;
    }

    if (visible_item_layout_matches_current())
    {
        refresh_visible_item_values();
#if defined(ESP_PLATFORM)
        ESP_LOGI(kLogTag,
                 "[UI][Txn] settings kind=value visible_clean=0 staging_commit=0 items=%u",
                 static_cast<unsigned>(g_state.item_count));
#endif
        return;
    }

    if (!begin_list_transaction())
    {
#if defined(ESP_PLATFORM)
        ESP_LOGE(kLogTag, "[UI][Txn] settings kind=structure staging_begin_failed");
#endif
        return;
    }
    s_building_list = true;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag,
             "build_item_list begin category=%d",
             g_state.current_category);
#endif
    g_state.list_back_btn = nullptr;
    g_state.item_count = 0;
    lv_obj_clear_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);

    const CategoryDef& cat = kCategories[g_state.current_category];
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag,
             "build_item_list category_label=%s item_count=%u",
             cat.label ? cat.label : "<null>",
             static_cast<unsigned>(cat.item_count));
#endif
    for (size_t i = 0; i < cat.item_count && g_state.item_count < kMaxItems; ++i)
    {
        settings::ui::ItemWidget& widget = g_state.item_widgets[g_state.item_count];
        widget.def = &cat.items[i];
        if (widget.def)
        {
            settings::ui::SettingItem* mutable_def =
                const_cast<settings::ui::SettingItem*>(widget.def);
            switch (::settings::ui::spec::dynamic_option_kind(item_id(*widget.def)))
            {
            case ::settings::ui::spec::DynamicOptionKind::ChatRegion:
                mutable_def->option_count = kChatRegionOptionCount;
                break;
            case ::settings::ui::spec::DynamicOptionKind::MeshCoreRegionPreset:
                mutable_def->option_count = kMeshCoreRegionPresetOptionCount;
                break;
            case ::settings::ui::spec::DynamicOptionKind::Locale:
                mutable_def->option_count = kLocaleOptionCount;
                break;
            case ::settings::ui::spec::DynamicOptionKind::TimeZone:
                mutable_def->option_count = kTimeZoneOptionCount;
                break;
            case ::settings::ui::spec::DynamicOptionKind::WifiNetwork:
                mutable_def->option_count = kWifiNetworkOptionCount;
                break;
            case ::settings::ui::spec::DynamicOptionKind::TxPower:
                mutable_def->option_count = kTxPowerOptionCount;
                break;
            case ::settings::ui::spec::DynamicOptionKind::None:
            default:
                break;
            }
        }
        if (!should_show_item(*widget.def))
        {
#if defined(ESP_PLATFORM)
            ESP_LOGI(kLogTag,
                     "build_item_list skip index=%u key=%s",
                     static_cast<unsigned>(i),
                     widget.def->pref_key ? widget.def->pref_key : "<none>");
#endif
            continue;
        }

#if defined(ESP_PLATFORM)
        ESP_LOGI(kLogTag,
                 "build_item_list item index=%u key=%s type=%d",
                 static_cast<unsigned>(i),
                 widget.def->pref_key ? widget.def->pref_key : "<none>",
                 static_cast<int>(widget.def->type));
#endif
        lv_obj_t* btn = lv_btn_create(g_state.list_content);
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
        g_state.list_back_btn = lv_btn_create(g_state.list_content);
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
    commit_list_transaction();
    settings::ui::input::on_ui_refreshed();
    lv_obj_scroll_to_y(g_state.list_panel, 0, LV_ANIM_OFF);
    lv_obj_invalidate(g_state.list_panel);
    lv_obj_add_flag(g_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_state.list_panel, LV_SCROLLBAR_MODE_AUTO);
    s_building_list = false;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag,
             "build_item_list complete visible_items=%u has_back=%d",
             static_cast<unsigned>(g_state.item_count),
             g_state.list_back_btn ? 1 : 0);
#endif
}

static void generate_meshtastic_channel_key(bool primary)
{
    app::IAppFacade& app_ctx = app::appFacade();
    auto edit = app_ctx.beginConfigEdit();
    if (!edit ||
        !::settings::ui::channel::generate_meshtastic_channel_key(
            edit.config(), g_settings, primary))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("PSK generation failed"), 3000);
        return;
    }
    edit.commit(app::AppConfigChangeSet::channels());
    app_ctx.applyMeshConfig();
    refresh_visible_item_values();
    ::ui::feedback::show_notice(::ui::i18n::tr("Channel PSK generated"), 2200);
}

static void generate_meshcore_channel_key()
{
    app::IAppFacade& app_ctx = app::appFacade();
    auto edit = app_ctx.beginConfigEdit();
    if (!edit ||
        !::settings::ui::channel::generate_meshcore_channel_key(edit.config(),
                                                                g_settings))
    {
        ::ui::feedback::show_notice(::ui::i18n::tr("Key generation failed"), 3000);
        return;
    }
    edit.commit(app::AppConfigChangeSet::channels());
    app_ctx.applyMeshConfig();
    refresh_visible_item_values();
    ::ui::feedback::show_notice(::ui::i18n::tr("Channel key generated"), 2200);
}

static void clear_meshcore_channel()
{
    app::IAppFacade& app_ctx = app::appFacade();
    const uint8_t slot =
        chat::normalizeMeshCoreChannelSlot(static_cast<uint8_t>(g_settings.mc_channel_slot));
    if (!commit_app_config(
            app_ctx,
            app::AppConfigChangeSet::channels(),
            [slot](app::AppConfig& config)
            {
                chat::MeshConfig& mesh = config.meshcore_config;
                chat::MeshCoreChannelConfig& channel = mesh.meshCoreChannel(slot);
                channel = chat::MeshCoreChannelConfig();
                if (slot == 0)
                {
                    channel.enabled = true;
                    copy_bounded(channel.name, sizeof(channel.name), "Public");
                }
                mesh.meshcore_channel_slot = slot;
                mesh.syncMeshCoreLegacyChannelMirror();
                ::settings::ui::channel::sync_meshcore_channel_fields(config,
                                                                      g_settings);
            }))
    {
        return;
    }
    app_ctx.applyMeshConfig();
    refresh_visible_item_values();
    ::ui::feedback::show_notice(::ui::i18n::tr("Channel cleared"), 1800);
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
    const settings::ui::SettingId id = item_id(item);
    if (item.type == settings::ui::SettingType::Toggle)
    {
        if (item.bool_value)
        {
            *item.bool_value = !(*item.bool_value);
            if (::settings::ui::spec::is_settings_store_owned_toggle(id))
            {
                prefs_put_bool(item.pref_key, *item.bool_value);
            }
            update_item_value(widget);
            switch (id)
            {
            case settings::ui::SettingId::NetRelay:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshtastic_config.enable_relay = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::MapTrack:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::map(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.map_track_enabled = value;
                    });
                apply_track_recording_runtime(*item.bool_value);
                break;
            }
            case settings::ui::SettingId::MapContour:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::map(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.map_contour_enabled = value;
                    });
                break;
            }
            case settings::ui::SettingId::GpsEnabled:
            {
                if (!apply_settings_bool_patch("gps_enabled", *item.bool_value))
                {
                    ::ui::feedback::show_notice(::ui::i18n::tr("Unable to apply GPS setting"), 3000);
                }
                break;
            }
            case settings::ui::SettingId::NetDutyCycle:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::network(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.net_duty_cycle = value;
                    });
                app_ctx.applyNetworkLimits();
                break;
            }
            case settings::ui::SettingId::NetTxEnabled:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        if (config.mesh_protocol == chat::MeshProtocol::MeshCore)
                        {
                            config.meshcore_config.tx_enabled = value;
                        }
                        else if (chat::infra::isReticulumMeshProtocol(
                                     config.mesh_protocol))
                        {
                            config.reticulumConfig().tx_enabled = value;
                        }
                        else
                        {
                            config.meshtastic_config.tx_enabled = value;
                        }
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::RtWifiAuto:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.reticulumConfig().reticulum_wifi_auto_connect =
                            value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::RtAnonymousPeer:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.reticulumConfig().reticulum_anonymous_peer =
                            value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::RtLocationRequests:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.reticulumConfig()
                            .reticulum_allow_location_requests = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::NetOverrideDuty:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshtastic_config.override_duty_cycle = value;
                    });
                app_ctx.applyMeshConfig();
                app_ctx.applyNetworkLimits();
                break;
            }
            case settings::ui::SettingId::McRepeat:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshcore_config.meshcore_client_repeat = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::McMultiAcks:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshcore_config.meshcore_multi_acks = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::McChannelEnabled:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                const uint8_t slot =
                    chat::normalizeMeshCoreChannelSlot(static_cast<uint8_t>(g_settings.mc_channel_slot));
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [slot, value = *item.bool_value](app::AppConfig& config)
                    {
                        chat::MeshConfig& mesh = config.meshcore_config;
                        chat::MeshCoreChannelConfig& channel =
                            mesh.meshCoreChannel(slot);
                        channel.enabled = (slot == 0) ? true : value;
                        mesh.meshcore_channel_slot = slot;
                        mesh.syncMeshCoreLegacyChannelMirror();
                        ::settings::ui::channel::sync_meshcore_channel_fields(
                            config, g_settings);
                    });
                app_ctx.applyMeshConfig();
                refresh_visible_item_values();
                break;
            }
            case settings::ui::SettingId::MtPrimaryEnabled:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.primary_enabled = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::MtPrimaryUplink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.primary_uplink_enabled = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::MtPrimaryDownlink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.primary_downlink_enabled = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::MtSecondaryEnabled:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.secondary_enabled = value;
                    });
                app_ctx.applyMeshConfig();
                build_item_list();
                break;
            }
            case settings::ui::SettingId::MtSecondaryUplink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.secondary_uplink_enabled = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::MtSecondaryDownlink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::channels(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.secondary_downlink_enabled = value;
                    });
                app_ctx.applyMeshConfig();
                break;
            }
            case settings::ui::SettingId::MtMqttEnabled:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshtastic_mqtt_enabled = value;
                    });
                build_item_list();
                break;
            }
            case settings::ui::SettingId::MtMqttUplink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshtastic_mqtt_uplink_enabled = value;
                    });
                break;
            }
            case settings::ui::SettingId::MtMqttDownlink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshtastic_mqtt_downlink_enabled = value;
                    });
                break;
            }
            case settings::ui::SettingId::McMqttEnabled:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshcore_config.meshcore_mqtt_enabled = value;
                    });
                build_item_list();
                break;
            }
            case settings::ui::SettingId::McMqttUplink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshcore_config.meshcore_mqtt_uplink_enabled =
                            value;
                    });
                break;
            }
            case settings::ui::SettingId::McMqttDownlink:
            {
                app::IAppFacade& app_ctx = app::appFacade();
                commit_app_config(
                    app_ctx,
                    app::AppConfigChangeSet::mesh(),
                    [value = *item.bool_value](app::AppConfig& config)
                    {
                        config.meshcore_config.meshcore_mqtt_downlink_enabled =
                            value;
                    });
                break;
            }
            case settings::ui::SettingId::WifiEnabled:
            {
                wifi_runtime::Config config{};
                config.enabled = *item.bool_value;
                copy_bounded(config.ssid, sizeof(config.ssid), g_settings.wifi_ssid);
                copy_bounded(config.password, sizeof(config.password), g_settings.wifi_password);
                (void)wifi_runtime::save_config(config);
                ScopedSettingsBusyOverlay busy(
                    ::ui::i18n::tr(config.enabled ? "Starting Wi-Fi..." : "Stopping Wi-Fi..."),
                    config.enabled ? g_settings.wifi_ssid : nullptr,
                    config.enabled ? 25 : 60);
                if (!wifi_runtime::apply_enabled(config.enabled) && config.enabled)
                {
                    ::ui::feedback::show_notice(::ui::i18n::tr("Wi-Fi start failed"), 3000);
                }
                if (!config.enabled)
                {
                    clear_wifi_scan_options();
                }
                refresh_wifi_state_from_runtime();
                build_item_list();
                break;
            }
            case settings::ui::SettingId::VibrationEnabled:
            {
                if (*item.bool_value)
                {
                    device_runtime::trigger_haptic();
                }
                break;
            }
            default:
                break;
            }
        }
        return true;
    }
    if (item.type == settings::ui::SettingType::Enum)
    {
        if (id == settings::ui::SettingId::DisplayLocale)
        {
            refresh_language_pack_options();
            update_item_value(widget);
        }
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
        switch (id)
        {
        case settings::ui::SettingId::MtPrimaryKeyGenerate:
            generate_meshtastic_channel_key(true);
            break;
        case settings::ui::SettingId::MtSecondaryKeyGenerate:
            generate_meshtastic_channel_key(false);
            break;
        case settings::ui::SettingId::McChannelKeyGenerate:
            generate_meshcore_channel_key();
            break;
        case settings::ui::SettingId::McChannelClear:
            clear_meshcore_channel();
            break;
        case settings::ui::SettingId::RtTcpDefaults:
        {
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
            if (::platform::ui::reticulum_network_config::restoreDefaultTcpEndpoints())
            {
                auto& app_ctx = app::appFacade();
                app_ctx.requestSaveConfig(app::AppConfigChangeSet::mesh());
                app_ctx.applyMeshConfig();
                g_settings.rt_tcp_slot = 0;
                refresh_reticulum_tcp_fields();
                refresh_visible_item_values();
                ::ui::feedback::show_notice(::ui::i18n::tr("Reticulum TCP defaults restored"), 3000);
            }
            else
            {
                ::ui::feedback::show_notice(::ui::i18n::tr("Unable to restore TCP defaults"), 3000);
            }
#endif
            break;
        }
        case settings::ui::SettingId::EnabledImes:
            refresh_language_pack_options();
            update_item_value(widget);
            open_enabled_imes_modal(widget);
            break;
        case settings::ui::SettingId::GpsDiagnostics:
            open_gps_diagnostics_modal();
            break;
        case settings::ui::SettingId::SpiDiagnostics:
            open_spi_diagnostics_modal();
            break;
        case settings::ui::SettingId::ChatResetMesh:
        {
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Resetting mesh..."),
                                           ::ui::i18n::tr("Applying defaults"),
                                           15);
            reset_mesh_settings();
            break;
        }
        case settings::ui::SettingId::ChatResetNodes:
        {
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Resetting nodes..."),
                                           ::ui::i18n::tr("Clearing node database"),
                                           35);
            reset_node_db();
            break;
        }
        case settings::ui::SettingId::ChatClearMessages:
        {
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Clearing messages..."),
                                           ::ui::i18n::tr("Updating chat storage"),
                                           35);
            clear_message_db();
            break;
        }
        case settings::ui::SettingId::SystemFactoryReset:
            open_factory_reset_modal();
            break;
        case settings::ui::SettingId::ManualTimeSet:
            open_manual_datetime_modal(widget);
            break;
        case settings::ui::SettingId::C6EnterDownload:
        {
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Preparing companion..."),
                                           ::ui::i18n::tr("Requesting download mode"),
                                           45);
            const bool ok = wireless_companion_runtime::enter_download_mode();
            refresh_wireless_companion_state_from_runtime();
            refresh_visible_item_values();
            ::ui::feedback::show_notice(
                ::ui::i18n::tr(ok ? "C6 download mode requested" : "C6 download mode failed"),
                ok ? 3000 : 4000);
            break;
        }
        case settings::ui::SettingId::WifiScan:
        {
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Scanning Wi-Fi..."),
                                           ::ui::i18n::tr("Looking for nearby networks"),
                                           20);
            clear_wifi_scan_options();
            auto& options = dynamic_options();
            size_t result_count = 0;
            if (!wifi_runtime::scan(options.wifi_scan_results, kMaxWifiNetworks, result_count))
            {
                refresh_wifi_state_from_runtime();
            }
            else
            {
                busy.update(::ui::i18n::tr("Scanning Wi-Fi..."),
                            ::ui::i18n::tr("Updating network list"),
                            80);
                rebuild_wifi_scan_options(result_count);
                for (size_t i = 0; i < kWifiNetworkOptionCount; ++i)
                {
                    if (std::strcmp(dynamic_options().wifi_scan_results[i].ssid,
                                    g_settings.wifi_ssid) == 0)
                    {
                        g_settings.wifi_network_index = static_cast<int>(i);
                        break;
                    }
                }
                refresh_wifi_state_from_runtime();
            }
            build_item_list();
            break;
        }
        case settings::ui::SettingId::WifiConnect:
        {
            wifi_runtime::Config config{};
            config.enabled = true;
            copy_bounded(config.ssid, sizeof(config.ssid), g_settings.wifi_ssid);
            copy_bounded(config.password, sizeof(config.password), g_settings.wifi_password);
            g_settings.wifi_enabled = true;
            (void)wifi_runtime::save_config(config);
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Connecting Wi-Fi..."),
                                           config.ssid,
                                           30);
            if (!wifi_runtime::apply_enabled(true) || !wifi_runtime::connect(&config))
            {
                refresh_wifi_state_from_runtime();
            }
            else
            {
                wifi_runtime::Status status = wifi_runtime::status();
                busy.update(::ui::i18n::tr("Wi-Fi connected"),
                            status.ip[0] != '\0' ? status.ip : status.ssid,
                            100);
                refresh_wifi_state_from_runtime();
            }
            build_item_list();
            break;
        }
        case settings::ui::SettingId::WifiDisconnect:
        {
            ScopedSettingsBusyOverlay busy(::ui::i18n::tr("Disconnecting Wi-Fi..."),
                                           g_settings.wifi_ssid,
                                           55);
            wifi_runtime::disconnect();
            refresh_wifi_state_from_runtime();
            build_item_list();
            break;
        }
        case settings::ui::SettingId::FwCheck:
        {
            if (!firmware_update_runtime::start_check())
            {
                sync_firmware_update_ui(false);
                const firmware_update_runtime::Status status = firmware_update_runtime::status();
                char message[160];
                if (status.busy)
                {
                    copy_bounded(message, sizeof(message), "Update task already running");
                }
                else if (status.message[0] != '\0')
                {
                    firmware_status_summary(status, message, sizeof(message));
                }
                else
                {
                    copy_bounded(message, sizeof(message), "Unable to start update check");
                }
                ::ui::feedback::show_notice(message, 3000);
            }
            else
            {
                sync_firmware_update_ui(false);
            }
            break;
        }
        case settings::ui::SettingId::FwInstall:
        {
            if (!firmware_update_runtime::start_install())
            {
                sync_firmware_update_ui(false);
                const firmware_update_runtime::Status status = firmware_update_runtime::status();
                char message[160];
                if (status.busy)
                {
                    copy_bounded(message, sizeof(message), "Update task already running");
                }
                else if (status.message[0] != '\0')
                {
                    firmware_status_summary(status, message, sizeof(message));
                }
                else
                {
                    copy_bounded(message, sizeof(message), "Unable to start OTA install");
                }
                ::ui::feedback::show_notice(message, 3000);
            }
            else
            {
                sync_firmware_update_ui(false);
            }
            break;
        }
        default:
            break;
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
    if (g_state.modal_root)
    {
        modal_close();
        return;
    }
    ui_request_exit_to_menu();
}

static void bind_dynamic_option_storage_to_items()
{
    auto& options = dynamic_options();
    for (const CategoryDef& category : kCategories)
    {
        settings::ui::spec::bind_items(
            const_cast<settings::ui::SettingItem*>(category.items),
            category.item_count);
        for (size_t index = 0; index < category.item_count; ++index)
        {
            settings::ui::SettingItem& item =
                const_cast<settings::ui::SettingItem&>(category.items[index]);
            switch (::settings::ui::spec::dynamic_option_kind(item.id))
            {
            case ::settings::ui::spec::DynamicOptionKind::ChatRegion:
                item.options = options.chat_region_options;
                break;
            case ::settings::ui::spec::DynamicOptionKind::MeshCoreRegionPreset:
                item.options = options.meshcore_region_preset_options;
                break;
            case ::settings::ui::spec::DynamicOptionKind::Locale:
                item.options = options.locale_options;
                break;
            case ::settings::ui::spec::DynamicOptionKind::TimeZone:
                item.options = options.time_zone_options;
                break;
            case ::settings::ui::spec::DynamicOptionKind::WifiNetwork:
                item.options = options.wifi_network_options;
                break;
            case ::settings::ui::spec::DynamicOptionKind::TxPower:
                item.options = options.tx_power_options;
                break;
            case ::settings::ui::spec::DynamicOptionKind::None:
            default:
                break;
            }
        }
    }
}

static void refresh_timezone_option_count()
{
    for (settings::ui::SettingItem& item : kDeviceItems)
    {
        if (item_id(item) == settings::ui::SettingId::TimezoneProfile)
        {
            item.option_count = kTimeZoneOptionCount;
            return;
        }
    }
}

} // namespace

void create(lv_obj_t* parent)
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create begin");
#endif
    refresh_timezone_options();
    refresh_timezone_option_count();
    settings_load();

    // Avoid auto-adding widgets to the current default group during creation.
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    g_state.parent = parent;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create root");
#endif
    g_state.root = layout::create_root(parent);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create header");
#endif
    layout::create_header(g_state.root, settings_back_cb, nullptr);

#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create content");
#endif
    g_state.content = layout::create_content(g_state.root);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create filter panel");
#endif
    layout::create_filter_panel(g_state.content);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create list panel");
#endif
    layout::create_list_panel(g_state.content);

    g_state.filter_count = sizeof(kCategories) / sizeof(kCategories[0]);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag,
             "create filter buttons count=%u",
             static_cast<unsigned>(g_state.filter_count));
#endif
    for (size_t i = 0; i < g_state.filter_count; ++i)
    {
#if defined(ESP_PLATFORM)
        ESP_LOGI(kLogTag,
                 "create filter button index=%u label=%s",
                 static_cast<unsigned>(i),
                 kCategories[i].label ? kCategories[i].label : "<null>");
#endif
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

#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create update_filter_styles");
#endif
    update_filter_styles();
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create build_item_list");
#endif
    build_item_list();
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create sync_firmware_update_ui");
#endif
    sync_firmware_update_ui(false);
    if (s_firmware_update_timer)
    {
        lv_timer_del(s_firmware_update_timer);
        s_firmware_update_timer = nullptr;
    }
#if !defined(ARDUINO_T_DECK_PRO)
    s_firmware_update_timer = lv_timer_create(firmware_update_timer_cb, 250, nullptr);
    if (s_firmware_update_timer)
    {
        lv_timer_set_repeat_count(s_firmware_update_timer, -1);
    }
#endif
    {
        const firmware_update_runtime::Status status = firmware_update_runtime::status();
        s_last_firmware_phase = status.phase;
        s_last_firmware_busy = status.busy;
    }

    // Restore previous default group before initializing input.
    set_default_group(prev_group);
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create input init");
#endif
    settings::ui::input::init();
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "create complete");
#endif
}

void destroy()
{
    if (g_state.modal_root)
    {
        modal_close(false);
    }
    if (s_firmware_update_timer)
    {
        lv_timer_del(s_firmware_update_timer);
        s_firmware_update_timer = nullptr;
    }
    ::ui::widgets::foreground_operation::clear(
        ::ui::widgets::foreground_operation::Slot::FirmwareUpdate);
    ::ui::widgets::foreground_operation::clear(
        ::ui::widgets::foreground_operation::Slot::SettingsAction);
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
