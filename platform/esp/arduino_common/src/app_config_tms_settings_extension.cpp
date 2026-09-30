#include "platform/esp/arduino_common/app_config_tms_settings_extension.h"
#include "platform/esp/arduino_common/app_config_tms_ble_extension.h"

#include "platform/ui/auto_reply_settings.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/wifi_runtime.h"

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
#include "platform/ui/a7682e_cellular_runtime.h"
#endif

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace app::sd_tms::settings_extension
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kPowerNs = "power";
constexpr const char* kDebugNs = "debug";
constexpr std::size_t kLocaleBytes = 48U;
constexpr std::size_t kEnabledImesBytes = 192U;
constexpr std::size_t kTimezoneTzdefBytes = 192U;
constexpr std::size_t kAutoReplyBytes = ::platform::ui::auto_reply::kTextMaxBytes;
constexpr std::size_t kNetworkInterfaceFieldCount = 8U;
constexpr std::size_t kNetworkPropagationFieldCount = 8U;
constexpr std::size_t kNetworkSeenCount =
    2U + ::chat::reticulum::kMaxNetworkInterfaces * kNetworkInterfaceFieldCount +
    kNetworkPropagationFieldCount;
static_assert(kNetworkSeenCount < 64U,
              "TMS Reticulum network presence mask must remain a scalar");
constexpr uint64_t kRequiredNetwork = (UINT64_C(1) << kNetworkSeenCount) - UINT64_C(1);

enum Seen : uint32_t
{
    SeenScreenTimeout = 1UL << 0U,
    SeenBrightness = 1UL << 1U,
    SeenSpeakerVolume = 1UL << 2U,
    SeenVibration = 1UL << 3U,
    SeenLocale = 1UL << 4U,
    SeenEnabledImes = 1UL << 5U,
    SeenTimezoneProfile = 1UL << 6U,
    SeenTimezoneOffset = 1UL << 7U,
    SeenTimezoneTzdef = 1UL << 8U,
    SeenMessageAlerts = 1UL << 9U,
    SeenContactAlerts = 1UL << 10U,
    SeenAutoReplyEnabled = 1UL << 11U,
    SeenAutoReplyText = 1UL << 12U,
    SeenDebugLogs = 1UL << 13U,
    SeenGaugeDesign = 1UL << 14U,
    SeenGaugeFull = 1UL << 15U,
    SeenWifiEnabled = 1UL << 16U,
    SeenWifiCount = 1UL << 17U,
};

constexpr uint32_t kRequiredBase = SeenScreenTimeout | SeenBrightness | SeenSpeakerVolume |
                                   SeenVibration | SeenLocale | SeenEnabledImes |
                                   SeenTimezoneProfile | SeenTimezoneOffset | SeenTimezoneTzdef |
                                   SeenMessageAlerts | SeenContactAlerts | SeenAutoReplyEnabled |
                                   SeenAutoReplyText | SeenDebugLogs | SeenGaugeDesign |
                                   SeenGaugeFull | SeenWifiEnabled | SeenWifiCount;

struct ParseState
{
    uint32_t seen = 0U;
    bool applying = false;
    uint32_t screen_timeout_ms = 30000U;
    int32_t screen_brightness = 0;
    int32_t speaker_volume = 45;
    bool vibration_enabled = true;
    char locale[kLocaleBytes]{};
    char enabled_imes[kEnabledImesBytes]{};
    std::size_t enabled_imes_len = 0U;
    int32_t timezone_profile = 0;
    int32_t timezone_offset_minutes = 0;
    uint8_t timezone_tzdef[kTimezoneTzdefBytes]{};
    std::size_t timezone_tzdef_len = 0U;
    int32_t message_alerts = 1;
    int32_t contact_alerts = 1;
    bool auto_reply_enabled = false;
    char auto_reply_text[kAutoReplyBytes + 1U]{};
    std::size_t auto_reply_text_len = 0U;
    bool debug_logs_enabled = false;
    uint32_t gauge_design_mah = 1500U;
    uint32_t gauge_full_mah = 1500U;
    bool wifi_enabled = false;
    uint8_t wifi_profile_count = 0U;
    bool wifi_ssid_seen[::platform::ui::wifi::kMaxSavedProfileCount]{};
    bool wifi_password_seen[::platform::ui::wifi::kMaxSavedProfileCount]{};
    ::platform::ui::wifi::Config wifi_profiles[::platform::ui::wifi::kMaxSavedProfileCount]{};
    uint64_t network_seen = 0U;
    uint32_t access_seen = 0U;
    ::chat::reticulum::ReticulumNetworkConfig network{};
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    uint32_t cellular_seen = 0U;
    ::platform::ui::a7682e::Config cellular{};
#else
    uint32_t cellular_seen = 0U;
#endif
};

static_assert(sizeof(ParseState) <= 4U * 1024U,
              "TMS Settings parse state must remain a bounded PSRAM allocation");

// A full document needs all ten Wi-Fi profiles until validation has completed.
// It must therefore be an atomic staging object, but it is needed only while a
// document is decoded or emitted.  Allocate it explicitly in PSRAM instead of
// trusting EXT_RAM_ATTR: the final linked image is the authority on placement.
ParseState* s_state_storage = nullptr;
bool s_state_allocation_failed_logged = false;

// Serial emission is sequential.  Locale, base64 strings, and timezone bytes
// can share one small 192-byte buffer rather than reserving two at all times.
uint8_t s_write_scratch[kTimezoneTzdefBytes]{};

#define s_state (*s_state_storage)

bool ensure_parse_state()
{
    if (s_state_storage)
    {
        return true;
    }

#if defined(ESP_PLATFORM)
    void* const raw = heap_caps_malloc(sizeof(ParseState),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const raw = std::malloc(sizeof(ParseState));
#endif
    s_state_storage = raw ? new (raw) ParseState{} : nullptr;
    if (s_state_storage)
    {
        return true;
    }

    if (!s_state_allocation_failed_logged)
    {
        std::printf("[TMS][Settings] parse_state allocation_failed memory=psram bytes=%u\n",
                    static_cast<unsigned>(sizeof(ParseState)));
        s_state_allocation_failed_logged = true;
    }
    return false;
}

void release_parse_state()
{
    if (!s_state_storage)
    {
        return;
    }
    s_state_storage->~ParseState();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_state_storage);
#else
    std::free(s_state_storage);
#endif
    s_state_storage = nullptr;
}

class ScopedWriteState
{
  public:
    ScopedWriteState()
        : ready_(ensure_parse_state())
    {
        if (ready_)
        {
            s_state = ParseState{};
        }
    }

    ~ScopedWriteState() { release_parse_state(); }

    bool ready() const { return ready_; }

  private:
    bool ready_ = false;
};

bool mark_once(uint32_t bit)
{
    if ((s_state.seen & bit) != 0U)
    {
        return false;
    }
    s_state.seen |= bit;
    return true;
}

bool key_equals(const tms::RecordReader& reader, const char* key)
{
    return std::strcmp(reader.key(), key) == 0;
}

bool valid_screen_timeout(uint32_t value)
{
    return value == 15000U || value == 30000U || value == 60000U || value == 300000U;
}

bool valid_timezone_offset_minutes(int32_t value)
{
    // Civil time zones range from UTC-12 through UTC+14.  Keeping this
    // bounded prevents a hand-edited raw value from reaching the platform
    // clock runtime while retaining every real-world offset.
    return value >= -720 && value <= 840;
}

bool valid_wifi_profile_set()
{
    if (s_state.wifi_profile_count > ::platform::ui::wifi::kMaxSavedProfileCount)
    {
        return false;
    }
    for (std::size_t index = 0U; index < ::platform::ui::wifi::kMaxSavedProfileCount; ++index)
    {
        const bool required = index < s_state.wifi_profile_count;
        if ((s_state.wifi_ssid_seen[index] || s_state.wifi_password_seen[index]) != required)
        {
            return false;
        }
        if (!required)
        {
            continue;
        }
        if (s_state.wifi_profiles[index].ssid[0] == '\0')
        {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior)
        {
            if (std::strcmp(s_state.wifi_profiles[index].ssid,
                            s_state.wifi_profiles[prior].ssid) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

bool mark_network_once(std::size_t index)
{
    if (index >= kNetworkSeenCount)
    {
        return false;
    }
    const uint64_t bit = UINT64_C(1) << index;
    if ((s_state.network_seen & bit) != 0U)
    {
        return false;
    }
    s_state.network_seen |= bit;
    return true;
}

bool read_network_interface_type(const tms::RecordReader& reader, std::size_t index)
{
    uint8_t value = 0U;
    if (index >= ::chat::reticulum::kMaxNetworkInterfaces || !reader.u8(&value, 2U))
    {
        return false;
    }
    s_state.network.interfaces[index].type =
        static_cast<::chat::reticulum::NetworkInterfaceType>(value);
    return true;
}

bool read_network_delivery(const tms::RecordReader& reader)
{
    uint8_t value = 0U;
    if (!reader.u8(&value, 2U))
    {
        return false;
    }
    s_state.network.propagation.delivery =
        static_cast<::chat::reticulum::LxmfDeliveryPreference>(value);
    return true;
}

tms::RecordConsumeResult consume_network_record(const tms::RecordReader& reader)
{
    if (key_equals(reader, "rt.net.version"))
    {
        uint16_t version = 0U;
        return mark_network_once(0U) && reader.u16(&version) && version == 1U
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.interface_count"))
    {
        return mark_network_once(1U) &&
                       reader.u8(&s_state.network.interface_count,
                                 static_cast<uint8_t>(::chat::reticulum::kMaxNetworkInterfaces))
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }

    for (std::size_t index = 0U; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
    {
        const std::size_t base = 2U + index * kNetworkInterfaceFieldCount;
        char key[48]{};
        const char* access_fields[] = {"network_name", "passphrase", "ifac_size"};
        for (unsigned field = 0; field < 3; ++field)
        {
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.%s", static_cast<unsigned>(index), access_fields[field]);
            if (!key_equals(reader, key)) continue;
            const uint32_t bit = 1U << (index * 3U + field);
            if (s_state.access_seen & bit) return tms::RecordConsumeResult::Invalid;
            s_state.access_seen |= bit;
            auto& access = s_state.network.interfaces[index].access;
            const bool valid = field == 0   ? reader.text(access.network_name, sizeof(access.network_name))
                               : field == 1 ? reader.text(access.passphrase, sizeof(access.passphrase))
                                            : reader.u16(&access.ifac_size_bits, 512U);
            return valid ? tms::RecordConsumeResult::Accepted : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.id", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base) &&
                           reader.text(s_state.network.interfaces[index].id,
                                       sizeof(s_state.network.interfaces[index].id))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.type", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 1U) && read_network_interface_type(reader, index)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.enabled", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 2U) &&
                           reader.boolean(&s_state.network.interfaces[index].enabled)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_host", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 3U) &&
                           reader.text(s_state.network.interfaces[index].target_host,
                                       sizeof(s_state.network.interfaces[index].target_host))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_port", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 4U) &&
                           reader.u16(&s_state.network.interfaces[index].target_port)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.group_id", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 5U) &&
                           reader.text(s_state.network.interfaces[index].group_id,
                                       sizeof(s_state.network.interfaces[index].group_id))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key,
                      sizeof(key),
                      "rt.net.interface.%u.discovery_port",
                      static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 6U) &&
                           reader.u16(&s_state.network.interfaces[index].discovery_port)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.data_port", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return mark_network_once(base + 7U) &&
                           reader.u16(&s_state.network.interfaces[index].data_port)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
    }

    constexpr std::size_t kPropagationBase =
        2U + ::chat::reticulum::kMaxNetworkInterfaces * kNetworkInterfaceFieldCount;
    if (key_equals(reader, "rt.net.propagation.enabled"))
    {
        return mark_network_once(kPropagationBase) &&
                       reader.boolean(&s_state.network.propagation.enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.service_enabled"))
    {
        return mark_network_once(kPropagationBase + 1U) &&
                       reader.boolean(&s_state.network.propagation.service_enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.delivery"))
    {
        return mark_network_once(kPropagationBase + 2U) && read_network_delivery(reader)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.automatic_node"))
    {
        return mark_network_once(kPropagationBase + 3U) &&
                       reader.boolean(&s_state.network.propagation.automatic_node)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.node_hash"))
    {
        std::size_t length = 0U;
        return mark_network_once(kPropagationBase + 4U) &&
                       reader.blob(s_state.network.propagation.node_hash,
                                   sizeof(s_state.network.propagation.node_hash),
                                   &length) &&
                       length == sizeof(s_state.network.propagation.node_hash)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.sync_on_start"))
    {
        return mark_network_once(kPropagationBase + 5U) &&
                       reader.boolean(&s_state.network.propagation.sync_on_start)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.sync_interval_s"))
    {
        return mark_network_once(kPropagationBase + 6U) &&
                       reader.u32(&s_state.network.propagation.sync_interval_s)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "rt.net.propagation.max_messages_per_sync"))
    {
        return mark_network_once(kPropagationBase + 7U) &&
                       reader.u8(&s_state.network.propagation.max_messages_per_sync, 64U)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    return tms::RecordConsumeResult::Unhandled;
}

bool valid_network_records(uint16_t schema_version)
{
    for (std::size_t index = 0; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
    {
        const auto fields = (s_state.access_seen >> (index * 3U)) & 7U;
        if (fields && (fields != 7U || index >= s_state.network.interface_count)) return false;
    }
    if (s_state.network_seen == 0U)
    {
        return schema_version != tms::kSchemaVersion;
    }
    return s_state.network_seen == kRequiredNetwork &&
           ::platform::ui::reticulum_network_config::validateForTms(s_state.network);
}

bool write_network_records(tms::RecordWriter& writer, const AppConfig& config)
{
    if (!::platform::ui::reticulum_network_config::snapshotForTms(config.reticulumConfig(),
                                                                  &s_state.network) ||
        !writer.u16("rt.net.version", 1U) ||
        !writer.u8("rt.net.interface_count", s_state.network.interface_count))
    {
        return false;
    }
    for (std::size_t index = 0U; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
    {
        const auto& interface_config = s_state.network.interfaces[index];
        char key[48]{};
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.id", static_cast<unsigned>(index));
        if (!writer.text(key, interface_config.id))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.type", static_cast<unsigned>(index));
        if (!writer.u8(key, static_cast<uint8_t>(interface_config.type)))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.enabled", static_cast<unsigned>(index));
        if (!writer.boolean(key, interface_config.enabled))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_host", static_cast<unsigned>(index));
        if (!writer.text(key, interface_config.target_host))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_port", static_cast<unsigned>(index));
        if (!writer.u16(key, interface_config.target_port))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.group_id", static_cast<unsigned>(index));
        if (!writer.text(key, interface_config.group_id))
        {
            return false;
        }
        std::snprintf(key,
                      sizeof(key),
                      "rt.net.interface.%u.discovery_port",
                      static_cast<unsigned>(index));
        if (!writer.u16(key, interface_config.discovery_port))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "rt.net.interface.%u.data_port", static_cast<unsigned>(index));
        if (!writer.u16(key, interface_config.data_port))
        {
            return false;
        }
        if (interface_config.access.enabled())
        {
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.network_name", static_cast<unsigned>(index));
            if (!writer.text(key, interface_config.access.network_name)) return false;
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.passphrase", static_cast<unsigned>(index));
            if (!writer.text(key, interface_config.access.passphrase)) return false;
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.ifac_size", static_cast<unsigned>(index));
            if (!writer.u16(key, interface_config.access.ifac_size_bits)) return false;
        }
    }
    const auto& propagation = s_state.network.propagation;
    return writer.boolean("rt.net.propagation.enabled", propagation.enabled) &&
           writer.boolean("rt.net.propagation.service_enabled", propagation.service_enabled) &&
           writer.u8("rt.net.propagation.delivery",
                     static_cast<uint8_t>(propagation.delivery)) &&
           writer.boolean("rt.net.propagation.automatic_node", propagation.automatic_node) &&
           writer.blob("rt.net.propagation.node_hash",
                       propagation.node_hash,
                       sizeof(propagation.node_hash)) &&
           writer.boolean("rt.net.propagation.sync_on_start", propagation.sync_on_start) &&
           writer.u32("rt.net.propagation.sync_interval_s", propagation.sync_interval_s) &&
           writer.u8("rt.net.propagation.max_messages_per_sync",
                     propagation.max_messages_per_sync);
}

bool valid_text_blob(const char* value, std::size_t length)
{
    return value != nullptr && std::memchr(value, '\0', length) == nullptr;
}

bool write_text_setting(tms::RecordWriter& writer,
                        const char* document_key,
                        const char* ns,
                        const char* store_key,
                        char* scratch,
                        std::size_t scratch_size)
{
    std::size_t length = 0U;
    if (!::platform::ui::settings_store::get_string_into(
            ns, store_key, scratch, scratch_size, &length))
    {
        scratch[0] = '\0';
    }
    return writer.text(document_key, scratch);
}

bool write_blob_setting(tms::RecordWriter& writer,
                        const char* document_key,
                        const char* ns,
                        const char* store_key)
{
    std::size_t length = 0U;
    if (!::platform::ui::settings_store::get_blob_into(
            ns, store_key, s_write_scratch, sizeof(s_write_scratch), &length))
    {
        length = 0U;
    }
    return writer.blob(document_key, s_write_scratch, length);
}

bool write_string_as_blob(tms::RecordWriter& writer,
                          const char* document_key,
                          const char* ns,
                          const char* store_key,
                          std::size_t maximum_length)
{
    if (maximum_length + 1U > sizeof(s_write_scratch))
    {
        return false;
    }
    std::size_t length = 0U;
    if (!::platform::ui::settings_store::get_string_into(
            ns,
            store_key,
            reinterpret_cast<char*>(s_write_scratch),
            maximum_length + 1U,
            &length))
    {
        length = 0U;
    }
    return writer.blob(document_key, s_write_scratch, length);
}

bool snapshot_wifi_profile(void*,
                           std::size_t index,
                           const ::platform::ui::wifi::Config& profile)
{
    if (index >= ::platform::ui::wifi::kMaxSavedProfileCount)
    {
        return false;
    }
    s_state.wifi_profiles[index] = profile;
    return true;
}

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
enum CellularSeen : uint32_t
{
    CellularEnabled = 1UL << 0U,
    CellularAutoAnswer = 1UL << 1U,
    CellularSpeakerGain = 1UL << 2U,
    CellularMicrophoneGain = 1UL << 3U,
    CellularSmtpPort = 1UL << 4U,
    CellularSmtpSecurity = 1UL << 5U,
    CellularApn = 1UL << 6U,
    CellularApnUser = 1UL << 7U,
    CellularApnPassword = 1UL << 8U,
    CellularSmsc = 1UL << 9U,
    CellularSmtpHost = 1UL << 10U,
    CellularSmtpUser = 1UL << 11U,
    CellularSmtpPassword = 1UL << 12U,
    CellularSmtpFrom = 1UL << 13U,
    CellularSmtpRecipient = 1UL << 14U,
};

constexpr uint32_t kRequiredCellular = CellularEnabled | CellularAutoAnswer | CellularSpeakerGain |
                                       CellularMicrophoneGain | CellularSmtpPort | CellularSmtpSecurity |
                                       CellularApn | CellularApnUser | CellularApnPassword | CellularSmsc |
                                       CellularSmtpHost | CellularSmtpUser | CellularSmtpPassword |
                                       CellularSmtpFrom | CellularSmtpRecipient;

bool mark_cellular_once(uint32_t bit)
{
    if ((s_state.cellular_seen & bit) != 0U)
    {
        return false;
    }
    s_state.cellular_seen |= bit;
    return true;
}

bool valid_cellular_config(const ::platform::ui::a7682e::Config& config)
{
    return config.speaker_gain <= 15U && config.microphone_gain <= 15U &&
           (config.smtp_port == 25U || config.smtp_port == 465U || config.smtp_port == 587U) &&
           config.smtp_security <= 2U;
}

bool write_cellular_records(tms::RecordWriter& writer)
{
    const auto& cellular = ::platform::ui::a7682e::config();
    return writer.boolean("cellular.enabled", cellular.enabled) &&
           writer.boolean("cellular.auto_answer", cellular.auto_answer) &&
           writer.u8("cellular.speaker_gain", cellular.speaker_gain) &&
           writer.u8("cellular.microphone_gain", cellular.microphone_gain) &&
           writer.u16("cellular.smtp_port", cellular.smtp_port) &&
           writer.u8("cellular.smtp_security", cellular.smtp_security) &&
           writer.text("cellular.apn", cellular.apn) &&
           writer.text("cellular.apn_user", cellular.apn_user) &&
           writer.text("cellular.apn_password", cellular.apn_password) &&
           writer.text("cellular.smsc", cellular.smsc) &&
           writer.text("cellular.smtp_host", cellular.smtp_host) &&
           writer.text("cellular.smtp_user", cellular.smtp_user) &&
           writer.text("cellular.smtp_password", cellular.smtp_password) &&
           writer.text("cellular.smtp_from", cellular.smtp_from) &&
           writer.text("cellular.smtp_default_recipient", cellular.smtp_default_recipient);
}
#endif

} // namespace

void beginRead(bool applying)
{
    if (!ensure_parse_state() || !ble_extension::beginRead())
    {
        release_parse_state();
        return;
    }
    s_state = ParseState{};
    s_state.applying = applying;
}

void endRead()
{
    ble_extension::endRead();
    release_parse_state();
}

tms::RecordConsumeResult consumeRecord(void*, const tms::RecordReader& reader)
{
    if (!s_state_storage)
    {
        return tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.screen.timeout_ms"))
    {
        return mark_once(SeenScreenTimeout) && reader.u32(&s_state.screen_timeout_ms)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.display.brightness"))
    {
        return mark_once(SeenBrightness) && reader.i32(&s_state.screen_brightness)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.audio.speaker_volume"))
    {
        return mark_once(SeenSpeakerVolume) && reader.i32(&s_state.speaker_volume)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.haptics.vibration_enabled"))
    {
        return mark_once(SeenVibration) && reader.boolean(&s_state.vibration_enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.locale"))
    {
        return mark_once(SeenLocale) && reader.text(s_state.locale, sizeof(s_state.locale))
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.ime.enabled"))
    {
        return mark_once(SeenEnabledImes) &&
                       reader.blob(reinterpret_cast<uint8_t*>(s_state.enabled_imes),
                                   sizeof(s_state.enabled_imes) - 1U,
                                   &s_state.enabled_imes_len)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.timezone.profile"))
    {
        return mark_once(SeenTimezoneProfile) && reader.i32(&s_state.timezone_profile)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.timezone.offset_minutes"))
    {
        return mark_once(SeenTimezoneOffset) && reader.i32(&s_state.timezone_offset_minutes)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "ui.timezone.tzdef"))
    {
        return mark_once(SeenTimezoneTzdef) &&
                       reader.blob(s_state.timezone_tzdef,
                                   sizeof(s_state.timezone_tzdef),
                                   &s_state.timezone_tzdef_len)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "chat.alerts.message"))
    {
        return mark_once(SeenMessageAlerts) && reader.i32(&s_state.message_alerts)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "chat.alerts.contact"))
    {
        return mark_once(SeenContactAlerts) && reader.i32(&s_state.contact_alerts)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "chat.auto_reply.enabled"))
    {
        return mark_once(SeenAutoReplyEnabled) && reader.boolean(&s_state.auto_reply_enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "chat.auto_reply.text"))
    {
        return mark_once(SeenAutoReplyText) &&
                       reader.blob(reinterpret_cast<uint8_t*>(s_state.auto_reply_text),
                                   kAutoReplyBytes,
                                   &s_state.auto_reply_text_len)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "debug.sd_logs_enabled"))
    {
        return mark_once(SeenDebugLogs) && reader.boolean(&s_state.debug_logs_enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "power.gauge.design_mah"))
    {
        return mark_once(SeenGaugeDesign) && reader.u32(&s_state.gauge_design_mah)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "power.gauge.full_mah"))
    {
        return mark_once(SeenGaugeFull) && reader.u32(&s_state.gauge_full_mah)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "wifi.enabled"))
    {
        return mark_once(SeenWifiEnabled) && reader.boolean(&s_state.wifi_enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "wifi.profile_count"))
    {
        return mark_once(SeenWifiCount) &&
                       reader.u8(&s_state.wifi_profile_count,
                                 static_cast<uint8_t>(::platform::ui::wifi::kMaxSavedProfileCount))
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    for (std::size_t index = 0U; index < ::platform::ui::wifi::kMaxSavedProfileCount; ++index)
    {
        char key[48]{};
        std::snprintf(key, sizeof(key), "wifi.profile.%u.ssid", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return !s_state.wifi_ssid_seen[index] &&
                           reader.text(s_state.wifi_profiles[index].ssid,
                                       sizeof(s_state.wifi_profiles[index].ssid))
                       ? (s_state.wifi_ssid_seen[index] = true,
                          tms::RecordConsumeResult::Accepted)
                       : tms::RecordConsumeResult::Invalid;
        }
        std::snprintf(key, sizeof(key), "wifi.profile.%u.password", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            return !s_state.wifi_password_seen[index] &&
                           reader.text(s_state.wifi_profiles[index].password,
                                       sizeof(s_state.wifi_profiles[index].password))
                       ? (s_state.wifi_password_seen[index] = true,
                          tms::RecordConsumeResult::Accepted)
                       : tms::RecordConsumeResult::Invalid;
        }
    }

    const tms::RecordConsumeResult network_result = consume_network_record(reader);
    if (network_result != tms::RecordConsumeResult::Unhandled)
    {
        return network_result;
    }

    const tms::RecordConsumeResult ble_result = ble_extension::consumeRecord(reader);
    if (ble_result != tms::RecordConsumeResult::Unhandled)
    {
        return ble_result;
    }

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
#define TMS_CELL_BOOL(name, field, bit)                                       \
    if (key_equals(reader, name))                                             \
    return mark_cellular_once(bit) && reader.boolean(&s_state.cellular.field) \
               ? tms::RecordConsumeResult::Accepted                           \
               : tms::RecordConsumeResult::Invalid
#define TMS_CELL_U8(name, field, bit)                                    \
    if (key_equals(reader, name))                                        \
    return mark_cellular_once(bit) && reader.u8(&s_state.cellular.field) \
               ? tms::RecordConsumeResult::Accepted                      \
               : tms::RecordConsumeResult::Invalid
#define TMS_CELL_U16(name, field, bit)                                    \
    if (key_equals(reader, name))                                         \
    return mark_cellular_once(bit) && reader.u16(&s_state.cellular.field) \
               ? tms::RecordConsumeResult::Accepted                       \
               : tms::RecordConsumeResult::Invalid
#define TMS_CELL_TEXT(name, field, bit)                                                \
    if (key_equals(reader, name))                                                      \
    return mark_cellular_once(bit) &&                                                  \
                   reader.text(s_state.cellular.field, sizeof(s_state.cellular.field)) \
               ? tms::RecordConsumeResult::Accepted                                    \
               : tms::RecordConsumeResult::Invalid
    TMS_CELL_BOOL("cellular.enabled", enabled, CellularEnabled);
    TMS_CELL_BOOL("cellular.auto_answer", auto_answer, CellularAutoAnswer);
    TMS_CELL_U8("cellular.speaker_gain", speaker_gain, CellularSpeakerGain);
    TMS_CELL_U8("cellular.microphone_gain", microphone_gain, CellularMicrophoneGain);
    TMS_CELL_U16("cellular.smtp_port", smtp_port, CellularSmtpPort);
    TMS_CELL_U8("cellular.smtp_security", smtp_security, CellularSmtpSecurity);
    TMS_CELL_TEXT("cellular.apn", apn, CellularApn);
    TMS_CELL_TEXT("cellular.apn_user", apn_user, CellularApnUser);
    TMS_CELL_TEXT("cellular.apn_password", apn_password, CellularApnPassword);
    TMS_CELL_TEXT("cellular.smsc", smsc, CellularSmsc);
    TMS_CELL_TEXT("cellular.smtp_host", smtp_host, CellularSmtpHost);
    TMS_CELL_TEXT("cellular.smtp_user", smtp_user, CellularSmtpUser);
    TMS_CELL_TEXT("cellular.smtp_password", smtp_password, CellularSmtpPassword);
    TMS_CELL_TEXT("cellular.smtp_from", smtp_from, CellularSmtpFrom);
    TMS_CELL_TEXT("cellular.smtp_default_recipient", smtp_default_recipient, CellularSmtpRecipient);
#undef TMS_CELL_BOOL
#undef TMS_CELL_U8
#undef TMS_CELL_U16
#undef TMS_CELL_TEXT
#endif

    return tms::RecordConsumeResult::Unhandled;
}

bool finishDocument(void*, bool applying, uint16_t schema_version)
{
    if (!s_state_storage)
    {
        return false;
    }
    const bool contains_extension_records =
        s_state.seen != 0U || s_state.network_seen != 0U || s_state.cellular_seen != 0U;
    if (schema_version == 2U && !contains_extension_records)
    {
        // TMSET2 predates the full Settings projection.  Its AppConfig is
        // still authoritative; existing independent NVS values are retained
        // and immediately upgraded to the current schema by the caller's
        // normal sync.
        return ble_extension::finishDocument(applying, schema_version);
    }
    if ((s_state.seen & kRequiredBase) != kRequiredBase ||
        !valid_screen_timeout(s_state.screen_timeout_ms) ||
        s_state.screen_brightness < 0 || s_state.screen_brightness > 255 ||
        s_state.speaker_volume < 0 || s_state.speaker_volume > 100 ||
        !valid_timezone_offset_minutes(s_state.timezone_offset_minutes) ||
        !valid_text_blob(s_state.enabled_imes, s_state.enabled_imes_len) ||
        s_state.message_alerts < 0 || s_state.message_alerts > 1 ||
        s_state.contact_alerts < 0 || s_state.contact_alerts > 2 ||
        s_state.gauge_design_mah == 0U || s_state.gauge_design_mah > 10000U ||
        s_state.gauge_full_mah == 0U || s_state.gauge_full_mah > 10000U ||
        !valid_wifi_profile_set() || !valid_network_records(schema_version))
    {
        return false;
    }
    if (!ble_extension::finishDocument(applying, schema_version))
    {
        return false;
    }
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    // Schema 3 is a complete working document on the hardware that owns this
    // block.  Accepting a missing cellular block here would silently retain
    // unrelated NVS values and break the SD-first authority guarantee.  The
    // schema-2 migration exception above remains intentionally permissive.
    if (s_state.cellular_seen != kRequiredCellular ||
        !valid_cellular_config(s_state.cellular))
    {
        return false;
    }
#endif
    if (!applying)
    {
        return true;
    }

    s_state.enabled_imes[s_state.enabled_imes_len] = '\0';
    s_state.auto_reply_text[s_state.auto_reply_text_len] = '\0';
    ::platform::ui::settings_store::put_uint(kSettingsNs,
                                             "screen_timeout",
                                             s_state.screen_timeout_ms);
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            "screen_brightness",
                                            static_cast<int>(s_state.screen_brightness));
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            "speaker_volume",
                                            static_cast<int>(s_state.speaker_volume));
    ::platform::ui::settings_store::put_bool(kSettingsNs,
                                             "vibration_enabled",
                                             s_state.vibration_enabled);
    bool ok = ::platform::ui::settings_store::put_string(
                  kSettingsNs, "display_locale", s_state.locale) &&
              ::platform::ui::settings_store::put_string(
                  kSettingsNs, "enabled_imes", s_state.enabled_imes) &&
              ::platform::ui::settings_store::put_blob(kSettingsNs,
                                                       "timezone_tzdef",
                                                       s_state.timezone_tzdef,
                                                       s_state.timezone_tzdef_len);
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            "timezone_profile",
                                            static_cast<int>(s_state.timezone_profile));
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            "timezone_offset",
                                            static_cast<int>(s_state.timezone_offset_minutes));
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            "chat_message_alerts",
                                            static_cast<int>(s_state.message_alerts));
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            "chat_contact_alerts",
                                            static_cast<int>(s_state.contact_alerts));
    ::platform::ui::settings_store::put_bool(kSettingsNs,
                                             ::platform::ui::auto_reply::kEnabledKey,
                                             s_state.auto_reply_enabled);
    ok = ::platform::ui::settings_store::put_string(
             kSettingsNs,
             ::platform::ui::auto_reply::kTextKey,
             s_state.auto_reply_text) &&
         ok;
    ::platform::ui::settings_store::put_bool(kSettingsNs,
                                             "adv_debug",
                                             s_state.debug_logs_enabled);
    ::platform::ui::settings_store::put_uint(kPowerNs,
                                             "gauge_design_mah",
                                             s_state.gauge_design_mah);
    ::platform::ui::settings_store::put_uint(kPowerNs,
                                             "gauge_full_mah",
                                             s_state.gauge_full_mah);
    ok = ::platform::ui::wifi::replace_saved_profiles(
             s_state.wifi_enabled, s_state.wifi_profiles, s_state.wifi_profile_count) &&
         ok;
    if (s_state.network_seen != 0U)
    {
        ok = ::platform::ui::reticulum_network_config::setFromTms(s_state.network) && ok;
    }
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    if (s_state.cellular_seen != 0U)
    {
        ok = ::platform::ui::a7682e::save_config(s_state.cellular) && ok;
    }
#endif
    return ok;
}

bool writeRecords(void* context, tms::RecordWriter& writer)
{
    const auto* const config = static_cast<const AppConfig*>(context);
    if (!config)
    {
        return false;
    }
    ScopedWriteState write_state;
    if (!write_state.ready())
    {
        return false;
    }
    if (!writer.u32("ui.screen.timeout_ms", ::platform::ui::screen::timeout_ms()) ||
        !writer.i32("ui.display.brightness",
                    ::platform::ui::settings_store::get_int(
                        kSettingsNs,
                        "screen_brightness",
                        static_cast<int>(::platform::ui::device::screen_brightness()))) ||
        !writer.i32("ui.audio.speaker_volume",
                    ::platform::ui::settings_store::get_int(kSettingsNs, "speaker_volume", 45)) ||
        !writer.boolean("ui.haptics.vibration_enabled",
                        ::platform::ui::settings_store::get_bool(
                            kSettingsNs, "vibration_enabled", true)) ||
        !write_text_setting(writer,
                            "ui.locale",
                            kSettingsNs,
                            "display_locale",
                            reinterpret_cast<char*>(s_write_scratch),
                            kLocaleBytes) ||
        !write_string_as_blob(writer,
                              "ui.ime.enabled",
                              kSettingsNs,
                              "enabled_imes",
                              kEnabledImesBytes - 1U) ||
        !writer.i32("ui.timezone.profile", ::platform::ui::time::timezone_profile_id()) ||
        !writer.i32("ui.timezone.offset_minutes", ::platform::ui::time::timezone_offset_min()) ||
        !write_blob_setting(writer,
                            "ui.timezone.tzdef",
                            kSettingsNs,
                            "timezone_tzdef") ||
        !writer.i32("chat.alerts.message",
                    ::platform::ui::settings_store::get_int(kSettingsNs, "chat_message_alerts", 1)) ||
        !writer.i32("chat.alerts.contact",
                    ::platform::ui::settings_store::get_int(kSettingsNs, "chat_contact_alerts", 1)) ||
        !writer.boolean("chat.auto_reply.enabled",
                        ::platform::ui::settings_store::get_bool(
                            kSettingsNs,
                            ::platform::ui::auto_reply::kEnabledKey,
                            false)) ||
        !write_string_as_blob(writer,
                              "chat.auto_reply.text",
                              kSettingsNs,
                              ::platform::ui::auto_reply::kTextKey,
                              kAutoReplyBytes) ||
        !writer.boolean("debug.sd_logs_enabled",
                        ::platform::ui::settings_store::get_bool(kSettingsNs, "adv_debug", false)) ||
        !writer.u32("power.gauge.design_mah",
                    ::platform::ui::settings_store::get_uint(kPowerNs, "gauge_design_mah", 1500U)) ||
        !writer.u32("power.gauge.full_mah",
                    ::platform::ui::settings_store::get_uint(kPowerNs, "gauge_full_mah", 1500U)) ||
        !write_network_records(writer, *config))
    {
        return false;
    }

    bool wifi_enabled = false;
    std::size_t wifi_count = 0U;
    if (!::platform::ui::wifi::visit_saved_profiles(
            &wifi_enabled, &wifi_count, snapshot_wifi_profile, nullptr) ||
        wifi_count > ::platform::ui::wifi::kMaxSavedProfileCount ||
        !writer.boolean("wifi.enabled", wifi_enabled) ||
        !writer.u8("wifi.profile_count", static_cast<uint8_t>(wifi_count)))
    {
        return false;
    }
    for (std::size_t index = 0U; index < wifi_count; ++index)
    {
        char key[48]{};
        std::snprintf(key, sizeof(key), "wifi.profile.%u.ssid", static_cast<unsigned>(index));
        if (!writer.text(key, s_state.wifi_profiles[index].ssid))
        {
            return false;
        }
        std::snprintf(key, sizeof(key), "wifi.profile.%u.password", static_cast<unsigned>(index));
        if (!writer.text(key, s_state.wifi_profiles[index].password))
        {
            return false;
        }
    }
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    if (!write_cellular_records(writer))
    {
        return false;
    }
#endif
    return ble_extension::writeRecords(writer);
}

#undef s_state

} // namespace app::sd_tms::settings_extension
