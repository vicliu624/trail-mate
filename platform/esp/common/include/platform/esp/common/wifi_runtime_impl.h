#pragma once

#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "bsp/m5stack_tab5.h"
#endif

namespace platform::ui::wifi
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";

struct RuntimeState
{
    bool stack_ready = false;
    bool handlers_registered = false;
    bool wifi_started = false;
    bool wifi_initialized = false;
    bool config_cached = false;
    bool connected = false;
    bool connecting = false;
    bool scanning = false;
    int rssi = -127;
    Config config{};
    char ssid[kMaxSsidLength + 1] = {};
    char ip[kMaxIpLength + 1] = {};
    char message[kMaxStatusMessageLength + 1] = {};
    esp_netif_t* sta_netif = nullptr;
    esp_event_handler_instance_t wifi_event_handler = nullptr;
    esp_event_handler_instance_t ip_event_handler = nullptr;
};

RuntimeState s_runtime{};

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const char* source = text ? text : "";
    std::snprintf(out, out_len, "%s", source);
}

void set_status_message(const char* message)
{
    copy_bounded(s_runtime.message, sizeof(s_runtime.message), message);
}

void cache_config(const Config& config)
{
    s_runtime.config = config;
    s_runtime.config_cached = true;
}

bool read_config_from_store(Config& out)
{
    out = Config{};
    out.enabled = ::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, value))
    {
        copy_bounded(out.ssid, sizeof(out.ssid), value.c_str());
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, value))
    {
        copy_bounded(out.password, sizeof(out.password), value.c_str());
    }

    cache_config(out);
    return true;
}

const Config& current_config()
{
    if (!s_runtime.config_cached)
    {
        Config config{};
        (void)read_config_from_store(config);
    }
    return s_runtime.config;
}

bool has_saved_credentials(const Config& config)
{
    return config.ssid[0] != '\0';
}

ConnectionState disconnected_state(bool enabled, bool has_credentials)
{
    if (!enabled)
    {
        return ConnectionState::Disabled;
    }
    return has_credentials ? ConnectionState::Idle : ConnectionState::Error;
}

void clear_connection_details()
{
    s_runtime.connected = false;
    s_runtime.connecting = false;
    s_runtime.rssi = -127;
    s_runtime.ssid[0] = '\0';
    s_runtime.ip[0] = '\0';
}

void refresh_runtime_status_message()
{
    const Config& config = s_runtime.config;

    if (!s_runtime.config_cached || !config.enabled)
    {
        set_status_message("Wi-Fi disabled");
        return;
    }

    if (s_runtime.scanning)
    {
        set_status_message("Scanning...");
        return;
    }

    if (s_runtime.connecting)
    {
        set_status_message("Connecting...");
        return;
    }

    if (s_runtime.connected)
    {
        if (s_runtime.ip[0] != '\0')
        {
            char buffer[kMaxStatusMessageLength + 1];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "Connected %s",
                          s_runtime.ip);
            set_status_message(buffer);
        }
        else
        {
            set_status_message("Connected");
        }
        return;
    }

    if (!has_saved_credentials(config))
    {
        set_status_message("Set SSID and password");
        return;
    }

    set_status_message("Ready to connect");
}

bool ensure_stack_ready()
{
    if (s_runtime.stack_ready)
    {
        return true;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    bsp_set_wifi_power_enable(true);
#endif

    const esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE)
    {
        set_status_message("esp_netif_init failed");
        return false;
    }

    const esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
    {
        set_status_message("event loop init failed");
        return false;
    }

    if (s_runtime.sta_netif == nullptr)
    {
        s_runtime.sta_netif = esp_netif_create_default_wifi_sta();
        if (s_runtime.sta_netif == nullptr)
        {
            set_status_message("STA netif create failed");
            return false;
        }
    }

    s_runtime.stack_ready = true;
    return true;
}

void wifi_event_handler(void*,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_CONNECTED:
            s_runtime.connecting = false;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            clear_connection_details();
            refresh_runtime_status_message();
            break;
        case WIFI_EVENT_SCAN_DONE:
            s_runtime.scanning = false;
            refresh_runtime_status_message();
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
        s_runtime.connected = true;
        s_runtime.connecting = false;
        if (got_ip)
        {
            std::snprintf(s_runtime.ip,
                          sizeof(s_runtime.ip),
                          IPSTR,
                          IP2STR(&got_ip->ip_info.ip));
        }
        wifi_ap_record_t ap_info{};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            s_runtime.rssi = ap_info.rssi;
            copy_bounded(s_runtime.ssid,
                         sizeof(s_runtime.ssid),
                         reinterpret_cast<const char*>(ap_info.ssid));
        }
        refresh_runtime_status_message();
    }
}

bool ensure_wifi_initialized()
{
    if (s_runtime.wifi_initialized)
    {
        return true;
    }

    if (!ensure_stack_ready())
    {
        return false;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    const esp_err_t init_err = esp_wifi_init(&config);
    if (init_err != ESP_OK && init_err != ESP_ERR_WIFI_INIT_STATE)
    {
        set_status_message("esp_wifi_init failed");
        return false;
    }

    const esp_err_t storage_err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (storage_err != ESP_OK)
    {
        set_status_message("wifi storage failed");
        return false;
    }

    const esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (mode_err != ESP_OK)
    {
        set_status_message("wifi mode failed");
        return false;
    }

    const esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ps_err != ESP_OK)
    {
        set_status_message("wifi power-save failed");
        return false;
    }

    if (!s_runtime.handlers_registered)
    {
        if (esp_event_handler_instance_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &wifi_event_handler,
                nullptr,
                &s_runtime.wifi_event_handler) != ESP_OK)
        {
            set_status_message("wifi event hook failed");
            return false;
        }

        if (esp_event_handler_instance_register(
                IP_EVENT,
                IP_EVENT_STA_GOT_IP,
                &wifi_event_handler,
                nullptr,
                &s_runtime.ip_event_handler) != ESP_OK)
        {
            set_status_message("ip event hook failed");
            return false;
        }

        s_runtime.handlers_registered = true;
    }

    s_runtime.wifi_initialized = true;
    return true;
}

bool ensure_wifi_started()
{
    if (s_runtime.wifi_started)
    {
        return true;
    }

    if (!ensure_wifi_initialized())
    {
        return false;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    bsp_set_wifi_power_enable(true);
#endif

    const esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN)
    {
        set_status_message("wifi start failed");
        return false;
    }

    s_runtime.wifi_started = true;
    refresh_runtime_status_message();
    return true;
}

} // namespace

bool is_supported()
{
    return true;
}

bool load_config(Config& out)
{
    return read_config_from_store(out);
}

bool save_config(const Config& config)
{
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    cache_config(config);
    refresh_runtime_status_message();
    return ssid_ok && password_ok;
}

bool apply_enabled(bool enabled)
{
    if (!is_supported())
    {
        return false;
    }

    if (!enabled)
    {
        if (s_runtime.wifi_started)
        {
            (void)esp_wifi_disconnect();
            (void)esp_wifi_stop();
        }
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
        bsp_set_wifi_power_enable(false);
#endif
        s_runtime.wifi_started = false;
        clear_connection_details();
        set_status_message("Wi-Fi disabled");
        return true;
    }

    if (!ensure_wifi_started())
    {
        return false;
    }

    clear_connection_details();
    refresh_runtime_status_message();
    return true;
}

bool connect(const Config* override_config)
{
    Config config{};
    if (override_config)
    {
        config = *override_config;
    }
    else
    {
        (void)load_config(config);
    }

    if (!config.enabled)
    {
        set_status_message("Enable Wi-Fi first");
        return false;
    }

    if (!has_saved_credentials(config))
    {
        set_status_message("SSID is not set");
        return false;
    }

    if (!ensure_wifi_started())
    {
        return false;
    }

    wifi_config_t wifi_config{};
    copy_bounded(reinterpret_cast<char*>(wifi_config.sta.ssid),
                 sizeof(wifi_config.sta.ssid),
                 config.ssid);
    copy_bounded(reinterpret_cast<char*>(wifi_config.sta.password),
                 sizeof(wifi_config.sta.password),
                 config.password);
    wifi_config.sta.threshold.authmode =
        config.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK)
    {
        set_status_message("Set Wi-Fi config failed");
        return false;
    }

    clear_connection_details();
    s_runtime.connecting = true;
    refresh_runtime_status_message();

    if (esp_wifi_disconnect() != ESP_OK)
    {
        // Ignore disconnect failures before a fresh connect attempt.
    }
    if (esp_wifi_connect() != ESP_OK)
    {
        s_runtime.connecting = false;
        set_status_message("Wi-Fi connect failed");
        return false;
    }

    constexpr int kConnectTimeoutMs = 15000;
    constexpr int kPollMs = 100;
    int waited_ms = 0;
    while (waited_ms < kConnectTimeoutMs)
    {
        if (s_runtime.connected)
        {
            refresh_runtime_status_message();
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
        waited_ms += kPollMs;
    }

    s_runtime.connecting = false;
    set_status_message("Wi-Fi connect timeout");
    return false;
}

void disconnect()
{
    if (s_runtime.wifi_started)
    {
        (void)esp_wifi_disconnect();
    }
    clear_connection_details();
    refresh_runtime_status_message();
}

bool scan(std::vector<ScanResult>& out_results)
{
    out_results.clear();

    Config config{};
    (void)load_config(config);
    if (!config.enabled)
    {
        set_status_message("Enable Wi-Fi first");
        return false;
    }

    if (!ensure_wifi_started())
    {
        return false;
    }

    s_runtime.scanning = true;
    refresh_runtime_status_message();

    wifi_scan_config_t scan_config{};
    const esp_err_t scan_err = esp_wifi_scan_start(&scan_config, true);
    s_runtime.scanning = false;
    if (scan_err != ESP_OK)
    {
        set_status_message("Wi-Fi scan failed");
        return false;
    }

    uint16_t ap_count = 0;
    if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK || ap_count == 0)
    {
        set_status_message("No Wi-Fi networks found");
        return true;
    }

    std::vector<wifi_ap_record_t> records(ap_count);
    if (esp_wifi_scan_get_ap_records(&ap_count, records.data()) != ESP_OK)
    {
        set_status_message("Read scan results failed");
        return false;
    }

    out_results.reserve(ap_count);
    for (const wifi_ap_record_t& record : records)
    {
        if (record.ssid[0] == 0)
        {
            continue;
        }

        ScanResult result{};
        copy_bounded(result.ssid,
                     sizeof(result.ssid),
                     reinterpret_cast<const char*>(record.ssid));
        result.rssi = record.rssi;
        result.requires_password = record.authmode != WIFI_AUTH_OPEN;
        out_results.push_back(result);
    }

    std::sort(out_results.begin(),
              out_results.end(),
              [](const ScanResult& lhs, const ScanResult& rhs)
              {
                  return lhs.rssi > rhs.rssi;
              });

    char buffer[kMaxStatusMessageLength + 1];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "Found %u networks",
                  static_cast<unsigned>(out_results.size()));
    set_status_message(buffer);
    return true;
}

Status status()
{
    Status out{};
    out.supported = is_supported();
    if (!out.supported)
    {
        copy_bounded(out.message, sizeof(out.message), "Wi-Fi unsupported");
        out.state = ConnectionState::Unsupported;
        return out;
    }

    const Config& config = current_config();
    out.enabled = config.enabled;
    out.has_credentials = has_saved_credentials(config);
    out.connected = s_runtime.connected;
    out.scanning = s_runtime.scanning;
    out.rssi = s_runtime.rssi;
    copy_bounded(out.ssid,
                 sizeof(out.ssid),
                 s_runtime.connected ? s_runtime.ssid : config.ssid);
    copy_bounded(out.ip, sizeof(out.ip), s_runtime.ip);

    if (!config.enabled)
    {
        out.state = ConnectionState::Disabled;
        copy_bounded(out.message, sizeof(out.message), "Wi-Fi disabled");
        return out;
    }

    if (s_runtime.scanning)
    {
        out.state = ConnectionState::Scanning;
        copy_bounded(out.message, sizeof(out.message), "Scanning...");
        return out;
    }

    if (s_runtime.connecting)
    {
        out.state = ConnectionState::Connecting;
        copy_bounded(out.message, sizeof(out.message), "Connecting...");
        return out;
    }

    if (s_runtime.connected)
    {
        out.state = ConnectionState::Connected;
        copy_bounded(out.message, sizeof(out.message), s_runtime.message);
        return out;
    }

    out.state = disconnected_state(config.enabled, out.has_credentials);
    if (out.has_credentials)
    {
        copy_bounded(out.message, sizeof(out.message), "Ready to connect");
    }
    else
    {
        copy_bounded(out.message, sizeof(out.message), "Set SSID and password");
    }
    return out;
}

} // namespace platform::ui::wifi
