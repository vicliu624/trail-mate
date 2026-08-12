#include "platform/ui/firmware_update_runtime.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "app/app_facade_access.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/http_client_runtime.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"

#ifndef TRAIL_MATE_ENABLE_BLE
#define TRAIL_MATE_ENABLE_BLE 0
#endif

#if TRAIL_MATE_ENABLE_BLE
#include "ble/ble_manager.h"
#endif

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif

namespace platform::ui::firmware_update
{
namespace
{

constexpr const char* kReleaseMetadataUrl = "https://vicliu624.github.io/trail-mate/data/latest-release.json";
constexpr const char* kReleaseBaseUrl = "https://vicliu624.github.io/trail-mate";
constexpr int kHttpBufferSize = 2048;
constexpr int kHttpTxBufferSize = 512;
constexpr std::size_t kMetadataLogSnippetBytes = 160;
constexpr std::size_t kOtaProgressLogStepBytes = 128 * 1024;
constexpr uint32_t kWorkerStackBytes = 12 * 1024;
constexpr UBaseType_t kWorkerPriority = 4;
constexpr int kMinBatteryPercentForInstall = 20;

enum class RequestedAction : uint8_t
{
    Check = 0,
    Install,
};

struct ParsedVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;
    bool valid = false;
};

struct ReleaseMetadata
{
    bool release_available = false;
    bool target_available = false;
    bool ota_available = false;
    std::string latest_version;
    std::string ota_path;
    std::string ota_sha256;
    std::size_t ota_size_bytes = 0;
};

struct WorkerContext
{
    RequestedAction action = RequestedAction::Check;
};

struct RuntimeState
{
    Status status{};
    TaskHandle_t worker_task = nullptr;
    bool launch_pending = false;
    bool initialized = false;
};

RuntimeState s_runtime{};
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void ota_log(const char* format, ...)
{
    std::printf("[OTA] ");
    va_list args;
    va_start(args, format);
    std::vprintf(format ? format : "", args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

const char* bool_text(bool value)
{
    return value ? "true" : "false";
}

const char* safe_text(const char* value)
{
    return value && value[0] != '\0' ? value : "(empty)";
}

const char* esp_err_name_safe(esp_err_t err)
{
    const char* name = esp_err_to_name(err);
    return name ? name : "ESP_ERR_UNKNOWN";
}

void set_esp_error(std::string& out_error, const char* message, esp_err_t err)
{
    char buffer[128];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%s: %s (0x%x)",
                  message ? message : "ESP error",
                  esp_err_name_safe(err),
                  static_cast<unsigned>(err));
    out_error = buffer;
}

const char* action_name(RequestedAction action)
{
    switch (action)
    {
    case RequestedAction::Check:
        return "check";
    case RequestedAction::Install:
        return "install";
    }
    return "unknown";
}

const char* wifi_state_name(::platform::ui::wifi::ConnectionState state)
{
    switch (state)
    {
    case ::platform::ui::wifi::ConnectionState::Unsupported:
        return "unsupported";
    case ::platform::ui::wifi::ConnectionState::Disabled:
        return "disabled";
    case ::platform::ui::wifi::ConnectionState::Idle:
        return "idle";
    case ::platform::ui::wifi::ConnectionState::Scanning:
        return "scanning";
    case ::platform::ui::wifi::ConnectionState::Connecting:
        return "connecting";
    case ::platform::ui::wifi::ConnectionState::ResourceDeferred:
        return "resource_deferred";
    case ::platform::ui::wifi::ConnectionState::Connected:
        return "connected";
    case ::platform::ui::wifi::ConnectionState::Error:
        return "error";
    }
    return "unknown";
}

std::string compact_log_snippet(const std::string& text)
{
    const std::size_t length = text.size() < kMetadataLogSnippetBytes ? text.size()
                                                                      : kMetadataLogSnippetBytes;
    std::string snippet = text.substr(0, length);
    for (char& ch : snippet)
    {
        if (ch == '\r' || ch == '\n' || ch == '\t')
        {
            ch = ' ';
        }
    }
    return snippet;
}

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

std::string lowercase_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool starts_with(const std::string& value, const char* prefix)
{
    if (!prefix)
    {
        return false;
    }
    const std::size_t prefix_len = std::strlen(prefix);
    return value.size() >= prefix_len && value.compare(0, prefix_len, prefix) == 0;
}

std::string trim_trailing_slash(std::string value)
{
    while (!value.empty() && value.back() == '/')
    {
        value.pop_back();
    }
    return value;
}

std::string join_url(const std::string& base, const std::string& path)
{
    if (path.empty())
    {
        return base;
    }
    if (starts_with(path, "http://") || starts_with(path, "https://"))
    {
        return path;
    }
    const std::string normalized_base = trim_trailing_slash(base);
    if (path.front() == '/')
    {
        return normalized_base + path;
    }
    return normalized_base + "/" + path;
}

std::string strip_version_prefix(std::string value)
{
    while (!value.empty() && !std::isdigit(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    return value;
}

ParsedVersion parse_version(const std::string& text)
{
    ParsedVersion version{};
    if (text.empty())
    {
        return version;
    }

    std::string numeric = strip_version_prefix(text);
    if (numeric.empty())
    {
        return version;
    }

    const std::size_t plus = numeric.find('+');
    if (plus != std::string::npos)
    {
        numeric = numeric.substr(0, plus);
    }

    const std::size_t dash = numeric.find('-');
    if (dash != std::string::npos)
    {
        version.prerelease = numeric.substr(dash + 1);
        numeric = numeric.substr(0, dash);
    }

    int parts[3] = {0, 0, 0};
    std::size_t part_index = 0;
    std::size_t start = 0;
    while (start <= numeric.size() && part_index < 3)
    {
        const std::size_t end = numeric.find('.', start);
        const std::string token = numeric.substr(start,
                                                 end == std::string::npos ? std::string::npos
                                                                          : (end - start));
        if (!token.empty())
        {
            char* parse_end = nullptr;
            const long parsed = std::strtol(token.c_str(), &parse_end, 10);
            if (parse_end != token.c_str())
            {
                parts[part_index] = static_cast<int>(parsed);
                version.valid = true;
            }
        }
        ++part_index;
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }

    version.major = parts[0];
    version.minor = parts[1];
    version.patch = parts[2];
    return version;
}

int compare_versions(const std::string& lhs, const std::string& rhs)
{
    const ParsedVersion left = parse_version(lhs);
    const ParsedVersion right = parse_version(rhs);

    if (left.major != right.major)
    {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor)
    {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.patch != right.patch)
    {
        return left.patch < right.patch ? -1 : 1;
    }
    if (left.prerelease == right.prerelease)
    {
        return 0;
    }
    if (left.prerelease.empty())
    {
        return 1;
    }
    if (right.prerelease.empty())
    {
        return -1;
    }
    return left.prerelease < right.prerelease ? -1 : 1;
}

const char* firmware_target_id()
{
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    return "tdeck-pro-a7682e";
#elif defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_PCM512A)
    return "tdeck-pro-pcm512a";
#elif defined(ARDUINO_T_DECK)
    return "tdeck";
#elif defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_SX1262)
    return "tlora-pager-sx1262";
#elif defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_LR1121)
    return "tlora-pager-lr1121";
#elif defined(ARDUINO_T_WATCH_S3)
    return "lilygo-twatch-s3";
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4_AMOLED)
    return "t-display-p4-amoled";
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return "t-display-p4-tft";
#else
    return nullptr;
#endif
}

bool is_supported_impl()
{
    return firmware_target_id() != nullptr && ::platform::ui::wifi::is_supported();
}

void ensure_initialized_locked()
{
    if (s_runtime.initialized)
    {
        return;
    }

    s_runtime.status = Status{};
    s_runtime.status.supported = is_supported_impl();
    s_runtime.status.direct_ota = s_runtime.status.supported;
    s_runtime.status.phase = s_runtime.status.supported ? Phase::Idle : Phase::Unsupported;
    copy_bounded(s_runtime.status.current_version,
                 sizeof(s_runtime.status.current_version),
                 ::platform::ui::device::firmware_version());
    copy_bounded(s_runtime.status.message,
                 sizeof(s_runtime.status.message),
                 s_runtime.status.supported ? "Ready to check" : "OTA unsupported");
    s_runtime.initialized = true;
}

template <typename Fn>
void with_locked_status(Fn&& fn)
{
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    fn(s_runtime.status);
    portEXIT_CRITICAL(&s_lock);
}

void refresh_current_version_locked(Status& status)
{
    copy_bounded(status.current_version,
                 sizeof(status.current_version),
                 ::platform::ui::device::firmware_version());
}

void set_status_locked(Status& status,
                       Phase phase,
                       bool busy,
                       const char* message,
                       const char* detail,
                       int progress_percent)
{
    refresh_current_version_locked(status);
    status.supported = is_supported_impl();
    status.direct_ota = status.supported;
    status.phase = phase;
    status.busy = busy;
    status.progress_percent = progress_percent;
    copy_bounded(status.message, sizeof(status.message), message);
    copy_bounded(status.detail, sizeof(status.detail), detail);
}

void set_error_status(const char* message, const char* detail = nullptr)
{
    with_locked_status(
        [message, detail](Status& status)
        {
            set_status_locked(status, Phase::Error, false, message ? message : "Update failed", detail, -1);
            status.checked = true;
            status.update_available = false;
        });
}

void set_checking_status(const char* detail)
{
    with_locked_status(
        [detail](Status& status)
        {
            set_status_locked(status, Phase::Checking, true, "Checking for updates...", detail, -1);
            status.checked = false;
            status.update_available = false;
            status.latest_version[0] = '\0';
        });
}

void set_update_available_status(const char* latest_version)
{
    with_locked_status(
        [latest_version](Status& status)
        {
            set_status_locked(status, Phase::UpdateAvailable, false, "Update available", latest_version, -1);
            status.checked = true;
            status.update_available = true;
            copy_bounded(status.latest_version, sizeof(status.latest_version), latest_version);
        });
}

void set_up_to_date_status(const char* latest_version)
{
    with_locked_status(
        [latest_version](Status& status)
        {
            set_status_locked(status, Phase::UpToDate, false, "Already up to date", latest_version, -1);
            status.checked = true;
            status.update_available = false;
            copy_bounded(status.latest_version, sizeof(status.latest_version), latest_version);
        });
}

void set_progress_status(Phase phase,
                         const char* message,
                         const char* detail,
                         int progress_percent)
{
    with_locked_status(
        [phase, message, detail, progress_percent](Status& status)
        {
            set_status_locked(status, phase, true, message, detail, progress_percent);
        });
}

void set_rebooting_status(const char* latest_version)
{
    with_locked_status(
        [latest_version](Status& status)
        {
            set_status_locked(status, Phase::Rebooting, true, "Restarting to finish update...", latest_version, 100);
            status.checked = true;
            status.update_available = false;
            copy_bounded(status.latest_version, sizeof(status.latest_version), latest_version);
        });
}

Status snapshot_status()
{
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    const Status copy = s_runtime.status;
    portEXIT_CRITICAL(&s_lock);
    return copy;
}

void worker_finished()
{
    portENTER_CRITICAL(&s_lock);
    s_runtime.worker_task = nullptr;
    s_runtime.launch_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

bool fetch_release_metadata_text(const std::string& url, std::string& out, std::string& out_error)
{
    out.clear();
    out_error.clear();

    ota_log("metadata http start url=%s", url.c_str());
    platform::ui::http_client::Request request{};
    request.url = url.c_str();
    request.client = platform::ui::wifi_access::Client::FirmwareUpdate;
    request.access_kind = platform::ui::wifi_access::AccessKind::HttpMetadata;
    request.priority = platform::ui::wifi_access::Priority::UserForeground;
    request.reason = "firmware_metadata";
    request.buffer_size = kHttpBufferSize;
    request.tx_buffer_size = kHttpTxBufferSize;

    platform::ui::http_client::TransferStats stats{};
    const bool ok = platform::ui::http_client::get_text(request, out, out_error, &stats);
    ota_log("metadata http finish ok=%s bytes=%u error=%s snippet=\"%s\"",
            bool_text(ok),
            static_cast<unsigned>(stats.bytes),
            out_error.empty() ? "(none)" : out_error.c_str(),
            ok ? compact_log_snippet(out).c_str() : "");
    return ok;
}

std::string json_string(cJSON* object, const char* key)
{
    if (!object || !key)
    {
        return {};
    }
    cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

bool json_bool(cJSON* object, const char* key, bool fallback = false)
{
    if (!object || !key)
    {
        return fallback;
    }
    cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsBool(value))
    {
        return cJSON_IsTrue(value);
    }
    return fallback;
}

std::size_t json_size_t(cJSON* object, const char* key, std::size_t fallback = 0)
{
    if (!object || !key)
    {
        return fallback;
    }
    cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value))
    {
        return fallback;
    }
    if (value->valuedouble <= 0)
    {
        return 0;
    }
    return static_cast<std::size_t>(value->valuedouble);
}

bool fetch_release_metadata(ReleaseMetadata& out_metadata, std::string& out_error)
{
    out_metadata = ReleaseMetadata{};
    out_error.clear();

    const auto wifi_status = ::platform::ui::wifi::status();
    ota_log("metadata wifi supported=%s enabled=%s connected=%s state=%s ssid=\"%s\" ip=%s rssi=%d message=\"%s\"",
            bool_text(wifi_status.supported),
            bool_text(wifi_status.enabled),
            bool_text(wifi_status.connected),
            wifi_state_name(wifi_status.state),
            safe_text(wifi_status.ssid),
            safe_text(wifi_status.ip),
            wifi_status.rssi,
            safe_text(wifi_status.message));
    if (!wifi_status.supported)
    {
        out_error = "Wi-Fi unsupported";
        ota_log("metadata rejected: %s", out_error.c_str());
        return false;
    }
    if (!wifi_status.connected)
    {
        char buffer[128];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "Wi-Fi not connected: %s",
                      wifi_state_name(wifi_status.state));
        out_error = buffer;
        ota_log("metadata rejected: %s message=\"%s\"",
                out_error.c_str(),
                safe_text(wifi_status.message));
        return false;
    }

    std::string text;
    if (!fetch_release_metadata_text(kReleaseMetadataUrl, text, out_error))
    {
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
    if (!root)
    {
        out_error = "Parse release metadata failed";
        ota_log("metadata json parse failed error_at=\"%s\" body_snippet=\"%s\"",
                safe_text(cJSON_GetErrorPtr()),
                compact_log_snippet(text).c_str());
        return false;
    }

    out_metadata.release_available = json_bool(root, "available");
    ota_log("metadata release available=%s tag=%s version=%s target=%s",
            bool_text(out_metadata.release_available),
            safe_text(json_string(root, "tag_name").c_str()),
            safe_text(json_string(root, "version").c_str()),
            safe_text(firmware_target_id()));
    if (!out_metadata.release_available)
    {
        cJSON_Delete(root);
        out_error = "No published release available";
        ota_log("metadata rejected: %s", out_error.c_str());
        return false;
    }

    const std::string top_level_version = strip_version_prefix(json_string(root, "version").empty()
                                                                   ? json_string(root, "tag_name")
                                                                   : json_string(root, "version"));
    cJSON* targets = cJSON_GetObjectItemCaseSensitive(root, "targets");
    cJSON* target = (targets && firmware_target_id()) ? cJSON_GetObjectItemCaseSensitive(targets, firmware_target_id())
                                                      : nullptr;
    if (!cJSON_IsObject(target))
    {
        cJSON_Delete(root);
        out_error = "No release published for this device";
        ota_log("metadata rejected: %s target=%s targets_object=%s",
                out_error.c_str(),
                safe_text(firmware_target_id()),
                bool_text(cJSON_IsObject(targets)));
        return false;
    }

    out_metadata.target_available = json_bool(target, "available");
    out_metadata.ota_available = json_bool(target, "ota_available");
    out_metadata.latest_version = strip_version_prefix(json_string(target, "version"));
    if (out_metadata.latest_version.empty())
    {
        out_metadata.latest_version = top_level_version;
    }
    out_metadata.ota_path = json_string(target, "ota_path");
    out_metadata.ota_sha256 = json_string(target, "ota_sha256");
    out_metadata.ota_size_bytes = json_size_t(target, "ota_size_bytes");

    ota_log("metadata target available=%s ota_available=%s version=%s path=%s sha_len=%u size=%u",
            bool_text(out_metadata.target_available),
            bool_text(out_metadata.ota_available),
            safe_text(out_metadata.latest_version.c_str()),
            safe_text(out_metadata.ota_path.c_str()),
            static_cast<unsigned>(out_metadata.ota_sha256.size()),
            static_cast<unsigned>(out_metadata.ota_size_bytes));

    cJSON_Delete(root);

    if (out_metadata.latest_version.empty())
    {
        out_error = "Release metadata missing version";
        ota_log("metadata rejected: %s", out_error.c_str());
        return false;
    }
    if (!out_metadata.ota_available || out_metadata.ota_path.empty())
    {
        out_error = "No OTA package for this device";
        ota_log("metadata rejected: %s ota_available=%s path=%s",
                out_error.c_str(),
                bool_text(out_metadata.ota_available),
                safe_text(out_metadata.ota_path.c_str()));
        return false;
    }
    if (out_metadata.ota_sha256.size() != 64)
    {
        out_error = out_metadata.ota_sha256.empty() ? "OTA metadata missing SHA256"
                                                    : "OTA metadata SHA256 invalid";
        ota_log("metadata rejected: %s sha_len=%u",
                out_error.c_str(),
                static_cast<unsigned>(out_metadata.ota_sha256.size()));
        return false;
    }
    ota_log("metadata accepted latest=%s ota_size=%u",
            safe_text(out_metadata.latest_version.c_str()),
            static_cast<unsigned>(out_metadata.ota_size_bytes));
    return true;
}

bool battery_allows_install(std::string& out_error)
{
    out_error.clear();
    const auto battery = ::platform::ui::device::battery_info();
    ota_log("install battery available=%s charging=%s level=%d min=%d",
            bool_text(battery.available),
            bool_text(battery.charging),
            battery.level,
            kMinBatteryPercentForInstall);
    if (!battery.available || battery.charging || battery.level < 0)
    {
        return true;
    }
    if (battery.level < kMinBatteryPercentForInstall)
    {
        out_error = "Charge battery before updating";
        ota_log("install rejected: %s", out_error.c_str());
        return false;
    }
    return true;
}

void restore_ble_after_failure(bool restore_ble)
{
#if TRAIL_MATE_ENABLE_BLE
    if (!restore_ble || !app::hasAppFacade())
    {
        return;
    }
    if (ble::BleManager* ble_manager = app::appFacade().getBleManager())
    {
        ble_manager->setEnabled(true);
    }
#else
    (void)restore_ble;
#endif
}

struct OtaDownloadContext
{
    esp_ota_handle_t ota_handle = 0;
    mbedtls_sha256_context* sha_ctx = nullptr;
    const ReleaseMetadata* metadata = nullptr;
    std::size_t bytes_written = 0;
    std::size_t next_progress_log = kOtaProgressLogStepBytes;
    int last_progress = -1;
    std::string error{};
};

class ScopedNonPreemptibleActivity
{
  public:
    explicit ScopedNonPreemptibleActivity(const char* reason)
    {
        platform::ui::wifi_access::set_non_preemptible_activity(true, reason);
    }

    ~ScopedNonPreemptibleActivity()
    {
        platform::ui::wifi_access::set_non_preemptible_activity(false);
    }

    ScopedNonPreemptibleActivity(const ScopedNonPreemptibleActivity&) = delete;
    ScopedNonPreemptibleActivity& operator=(const ScopedNonPreemptibleActivity&) = delete;
};

bool write_ota_chunk(const std::uint8_t* data, std::size_t len, void* context)
{
    auto* ota = static_cast<OtaDownloadContext*>(context);
    if (!ota || !data || len == 0 || !ota->sha_ctx || !ota->metadata)
    {
        return false;
    }

    ScopedNonPreemptibleActivity non_preemptible("firmware_ota_write");
    const esp_err_t write_err = esp_ota_write(ota->ota_handle, data, len);
    if (write_err != ESP_OK)
    {
        set_esp_error(ota->error, "Write OTA image failed", write_err);
        ota_log("ota write failed err=%s (0x%x) offset=%u read=%u",
                esp_err_name_safe(write_err),
                static_cast<unsigned>(write_err),
                static_cast<unsigned>(ota->bytes_written),
                static_cast<unsigned>(len));
        return false;
    }

    mbedtls_sha256_update(ota->sha_ctx, data, len);
    ota->bytes_written += len;

    const std::size_t progress_total = ota->metadata->ota_size_bytes;
    if (progress_total > 0)
    {
        int progress = static_cast<int>((ota->bytes_written * 100U) / progress_total);
        if (progress > 100)
        {
            progress = 100;
        }
        if (progress != ota->last_progress)
        {
            ota->last_progress = progress;
            char detail[32];
            std::snprintf(detail, sizeof(detail), "%d%%", progress);
            set_progress_status(Phase::Downloading, "Downloading update...", detail, progress);
        }
    }
    if (ota->bytes_written >= ota->next_progress_log)
    {
        ota_log("ota progress bytes=%u total=%u",
                static_cast<unsigned>(ota->bytes_written),
                static_cast<unsigned>(progress_total));
        ota->next_progress_log += kOtaProgressLogStepBytes;
    }
    return true;
}

bool begin_ota_download(const ReleaseMetadata& metadata, std::string& out_error)
{
    out_error.clear();

    const std::string url = join_url(kReleaseBaseUrl, metadata.ota_path);
    ota_log("ota download start url=%s expected_size=%u expected_sha=%s",
            url.c_str(),
            static_cast<unsigned>(metadata.ota_size_bytes),
            safe_text(metadata.ota_sha256.c_str()));

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition)
    {
        out_error = "No OTA partition available";
        ota_log("ota rejected: %s", out_error.c_str());
        return false;
    }
    ota_log("ota partition label=%s address=0x%x size=%u subtype=%u",
            update_partition->label,
            static_cast<unsigned>(update_partition->address),
            static_cast<unsigned>(update_partition->size),
            static_cast<unsigned>(update_partition->subtype));

    if (metadata.ota_size_bytes > 0 && metadata.ota_size_bytes > update_partition->size)
    {
        out_error = "Firmware image is too large";
        ota_log("ota rejected: %s image_size=%u partition_size=%u",
                out_error.c_str(),
                static_cast<unsigned>(metadata.ota_size_bytes),
                static_cast<unsigned>(update_partition->size));
        return false;
    }

    esp_ota_handle_t ota_handle = 0;
    bool ota_started = false;
    bool ok = false;
    OtaDownloadContext ota_context{};
    platform::ui::http_client::Request request{};
    platform::ui::http_client::TransferStats stats{};

    mbedtls_sha256_context sha_ctx;
    unsigned char hash[32];
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    {
        const esp_err_t begin_err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
        if (begin_err != ESP_OK)
        {
            set_esp_error(out_error, "Begin OTA write failed", begin_err);
            ota_log("ota begin failed err=%s (0x%x)",
                    esp_err_name_safe(begin_err),
                    static_cast<unsigned>(begin_err));
            goto cleanup;
        }
    }
    ota_log("ota begin ok");
    ota_started = true;

    set_progress_status(Phase::Downloading, "Downloading update...", "0%", 0);

    ota_context.ota_handle = ota_handle;
    ota_context.sha_ctx = &sha_ctx;
    ota_context.metadata = &metadata;

    request.url = url.c_str();
    request.client = platform::ui::wifi_access::Client::FirmwareUpdate;
    request.access_kind = platform::ui::wifi_access::AccessKind::OtaDownload;
    request.priority = platform::ui::wifi_access::Priority::UserForeground;
    request.reason = "firmware_ota";
    request.buffer_size = kHttpBufferSize;
    request.tx_buffer_size = kHttpTxBufferSize;
    request.max_bytes = update_partition->size;

    if (!platform::ui::http_client::download(request,
                                             &write_ota_chunk,
                                             &ota_context,
                                             out_error,
                                             &stats))
    {
        if (!ota_context.error.empty())
        {
            out_error = ota_context.error;
        }
        ota_log("ota http download failed status=%d bytes=%u error=%s",
                stats.http_status,
                static_cast<unsigned>(ota_context.bytes_written),
                out_error.empty() ? "(none)" : out_error.c_str());
        goto cleanup;
    }

    ota_log("ota download complete bytes=%u http_status=%d",
            static_cast<unsigned>(ota_context.bytes_written),
            stats.http_status);

    if (ota_context.bytes_written == 0)
    {
        out_error = "OTA image download was empty";
        ota_log("ota rejected: %s", out_error.c_str());
        goto cleanup;
    }
    if (metadata.ota_size_bytes > 0 && ota_context.bytes_written != metadata.ota_size_bytes)
    {
        out_error = "OTA image size mismatch";
        ota_log("ota rejected: %s expected=%u actual=%u",
                out_error.c_str(),
                static_cast<unsigned>(metadata.ota_size_bytes),
                static_cast<unsigned>(ota_context.bytes_written));
        goto cleanup;
    }

    mbedtls_sha256_finish(&sha_ctx, hash);
    {
        char actual_sha256[65];
        for (int i = 0; i < 32; ++i)
        {
            std::snprintf(actual_sha256 + (i * 2), 3, "%02x", hash[i]);
        }
        actual_sha256[64] = '\0';
        if (lowercase_ascii(actual_sha256) != lowercase_ascii(metadata.ota_sha256))
        {
            out_error = "OTA SHA256 mismatch";
            ota_log("ota sha mismatch expected=%s actual=%s",
                    safe_text(metadata.ota_sha256.c_str()),
                    actual_sha256);
            goto cleanup;
        }
        ota_log("ota sha ok actual=%s", actual_sha256);
    }

    set_progress_status(Phase::Installing, "Verifying update...", "Finalizing image", 100);
    {
        ScopedNonPreemptibleActivity non_preemptible("firmware_ota_finalize");
        const esp_err_t end_err = esp_ota_end(ota_handle);
        if (end_err != ESP_OK)
        {
            set_esp_error(out_error, "Finalize OTA image failed", end_err);
            ota_log("ota end failed err=%s (0x%x)",
                    esp_err_name_safe(end_err),
                    static_cast<unsigned>(end_err));
            goto cleanup;
        }
    }
    ota_log("ota end ok");
    ota_started = false;

    {
        ScopedNonPreemptibleActivity non_preemptible("firmware_ota_activate");
        const esp_err_t boot_err = esp_ota_set_boot_partition(update_partition);
        if (boot_err != ESP_OK)
        {
            set_esp_error(out_error, "Activate OTA partition failed", boot_err);
            ota_log("ota set boot partition failed err=%s (0x%x)",
                    esp_err_name_safe(boot_err),
                    static_cast<unsigned>(boot_err));
            goto cleanup;
        }
    }
    ota_log("ota set boot partition ok");

    ok = true;

cleanup:
    if (ota_started)
    {
        const esp_err_t abort_err = esp_ota_abort(ota_handle);
        ota_log("ota abort err=%s (0x%x)",
                esp_err_name_safe(abort_err),
                static_cast<unsigned>(abort_err));
    }
    mbedtls_sha256_free(&sha_ctx);
    ota_log("ota download finish ok=%s bytes=%u error=%s",
            bool_text(ok),
            static_cast<unsigned>(ota_context.bytes_written),
            out_error.empty() ? "(none)" : out_error.c_str());
    return ok;
}

bool perform_check(std::string& out_error)
{
    ota_log("check start current=%s target=%s metadata_url=%s",
            safe_text(::platform::ui::device::firmware_version()),
            safe_text(firmware_target_id()),
            kReleaseMetadataUrl);
    ReleaseMetadata metadata{};
    if (!fetch_release_metadata(metadata, out_error))
    {
        ota_log("check failed stage=metadata error=%s", safe_text(out_error.c_str()));
        return false;
    }

    const char* current_version = ::platform::ui::device::firmware_version();
    const int compare_result = compare_versions(current_version ? current_version : "", metadata.latest_version);
    ota_log("check compare current=%s latest=%s result=%d",
            safe_text(current_version),
            safe_text(metadata.latest_version.c_str()),
            compare_result);
    if (compare_result < 0)
    {
        set_update_available_status(metadata.latest_version.c_str());
        ota_log("check result update_available latest=%s", safe_text(metadata.latest_version.c_str()));
    }
    else
    {
        set_up_to_date_status(metadata.latest_version.c_str());
        ota_log("check result up_to_date latest=%s", safe_text(metadata.latest_version.c_str()));
    }
    return true;
}

bool perform_install(std::string& out_error)
{
    ota_log("install start current=%s target=%s",
            safe_text(::platform::ui::device::firmware_version()),
            safe_text(firmware_target_id()));
    ReleaseMetadata metadata{};
    if (!fetch_release_metadata(metadata, out_error))
    {
        ota_log("install failed stage=metadata error=%s", safe_text(out_error.c_str()));
        return false;
    }

    const char* current_version = ::platform::ui::device::firmware_version();
    const int compare_result = compare_versions(current_version ? current_version : "", metadata.latest_version);
    ota_log("install compare current=%s latest=%s result=%d",
            safe_text(current_version),
            safe_text(metadata.latest_version.c_str()),
            compare_result);
    if (compare_result >= 0)
    {
        set_up_to_date_status(metadata.latest_version.c_str());
        ota_log("install skipped already_up_to_date latest=%s", safe_text(metadata.latest_version.c_str()));
        return true;
    }

    if (!battery_allows_install(out_error))
    {
        ota_log("install failed stage=battery error=%s", safe_text(out_error.c_str()));
        return false;
    }

    bool restore_ble = false;
#if TRAIL_MATE_ENABLE_BLE
    if (app::hasAppFacade())
    {
        if (ble::BleManager* ble_manager = app::appFacade().getBleManager())
        {
            restore_ble = ble_manager->isEnabled();
            if (restore_ble)
            {
                set_progress_status(Phase::Installing, "Preparing update...", "Stopping BLE service", -1);
                ota_log("install stopping BLE before OTA");
                ble_manager->setEnabled(false);
            }
        }
    }
#endif

    const bool ok = begin_ota_download(metadata, out_error);
    if (!ok)
    {
        restore_ble_after_failure(restore_ble);
        ota_log("install failed stage=download error=%s ble_restored=%s",
                safe_text(out_error.c_str()),
                bool_text(restore_ble));
        return false;
    }

    set_rebooting_status(metadata.latest_version.c_str());
    ota_log("install success latest=%s rebooting", safe_text(metadata.latest_version.c_str()));
    vTaskDelay(pdMS_TO_TICKS(600));
    ::platform::ui::device::restart();
    return true;
}

void worker_task_entry(void* param)
{
    WorkerContext* ctx = static_cast<WorkerContext*>(param);
    RequestedAction action = RequestedAction::Check;
    if (ctx)
    {
        action = ctx->action;
        delete ctx;
    }

    ota_log("worker start action=%s", action_name(action));
    std::string error;
    bool ok = false;
    switch (action)
    {
    case RequestedAction::Check:
        ok = perform_check(error);
        break;
    case RequestedAction::Install:
        set_checking_status("Refreshing release metadata");
        ok = perform_install(error);
        break;
    }

    if (!ok)
    {
        set_error_status(action == RequestedAction::Check ? "Update check failed" : "Update install failed",
                         error.c_str());
    }

    ota_log("worker finish action=%s ok=%s error=%s",
            action_name(action),
            bool_text(ok),
            error.empty() ? "(none)" : error.c_str());
    worker_finished();
    vTaskDelete(nullptr);
}

bool queue_worker(RequestedAction action, const char* initial_detail)
{
    ota_log("queue request action=%s target=%s current=%s wifi_supported=%s stack=%lu priority=%u",
            action_name(action),
            safe_text(firmware_target_id()),
            safe_text(::platform::ui::device::firmware_version()),
            bool_text(::platform::ui::wifi::is_supported()),
            static_cast<unsigned long>(kWorkerStackBytes),
            static_cast<unsigned>(kWorkerPriority));
    WorkerContext* ctx = new (std::nothrow) WorkerContext{};
    if (!ctx)
    {
        set_error_status("Allocate update worker failed");
        ota_log("queue rejected action=%s reason=alloc_failed", action_name(action));
        return false;
    }
    ctx->action = action;

    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    const bool supported = s_runtime.status.supported;
    const bool has_worker = s_runtime.worker_task != nullptr;
    const bool launch_pending = s_runtime.launch_pending;
    char status_message[sizeof(s_runtime.status.message)] = {};
    char status_detail[sizeof(s_runtime.status.detail)] = {};
    copy_bounded(status_message, sizeof(status_message), s_runtime.status.message);
    copy_bounded(status_detail, sizeof(status_detail), s_runtime.status.detail);
    if (!supported || has_worker || launch_pending)
    {
        portEXIT_CRITICAL(&s_lock);
        delete ctx;
        ota_log("queue rejected action=%s supported=%s has_worker=%s launch_pending=%s message=%s detail=%s",
                action_name(action),
                bool_text(supported),
                bool_text(has_worker),
                bool_text(launch_pending),
                safe_text(status_message),
                safe_text(status_detail));
        return false;
    }
    s_runtime.launch_pending = true;
    set_status_locked(s_runtime.status,
                      Phase::Checking,
                      true,
                      action == RequestedAction::Install ? "Preparing update..." : "Checking for updates...",
                      initial_detail,
                      -1);
    s_runtime.status.checked = false;
    s_runtime.status.update_available = false;
    portEXIT_CRITICAL(&s_lock);

    TaskHandle_t task_handle = nullptr;
    BaseType_t task_ok =
        xTaskCreate(worker_task_entry, "fw_update", kWorkerStackBytes, ctx, kWorkerPriority, &task_handle);

    portENTER_CRITICAL(&s_lock);
    if (task_ok != pdPASS || task_handle == nullptr)
    {
        s_runtime.launch_pending = false;
        s_runtime.worker_task = nullptr;
        set_status_locked(s_runtime.status, Phase::Error, false, "Create update task failed", nullptr, -1);
        portEXIT_CRITICAL(&s_lock);
        delete ctx;
        ota_log("queue rejected action=%s reason=create_task_failed result=%d handle=%p",
                action_name(action),
                static_cast<int>(task_ok),
                static_cast<void*>(task_handle));
        return false;
    }
    const bool worker_finished_before_handle_store = !s_runtime.launch_pending;
    if (!worker_finished_before_handle_store)
    {
        s_runtime.worker_task = task_handle;
        s_runtime.launch_pending = false;
    }
    portEXIT_CRITICAL(&s_lock);
    ota_log("queue accepted action=%s task=%p already_finished=%s",
            action_name(action),
            static_cast<void*>(task_handle),
            bool_text(worker_finished_before_handle_store));
    return true;
}

} // namespace

bool is_supported()
{
    return snapshot_status().supported;
}

Status status()
{
    return snapshot_status();
}

bool start_check()
{
    return queue_worker(RequestedAction::Check, "Contacting GitHub Pages");
}

bool start_install()
{
    return queue_worker(RequestedAction::Install, "Contacting GitHub Pages");
}

} // namespace platform::ui::firmware_update
