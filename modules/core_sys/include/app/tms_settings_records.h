#pragma once

#include "app/tms_config_codec.h"
#include "platform/ui/auto_reply_settings.h"
#include "platform/ui/wifi_runtime.h"
#include <cstdio>
#include <cstring>

namespace app::tms
{
// Pure, caller-owned staging for the platform-independent Settings records.
// Storage, allocation and runtime application belong to platform adapters.
struct SettingsRecords
{
    static constexpr size_t kLocaleBytes = 48;
    static constexpr size_t kEnabledImesBytes = 192;
    static constexpr size_t kTimezoneTzdefBytes = 192;
    static constexpr size_t kAutoReplyBytes = platform::ui::auto_reply::kTextMaxBytes;
    enum Seen : uint32_t
    {
        SeenScreenTimeout = 1UL << 0,
        SeenBrightness = 1UL << 1,
        SeenSpeakerVolume = 1UL << 2,
        SeenVibration = 1UL << 3,
        SeenLocale = 1UL << 4,
        SeenEnabledImes = 1UL << 5,
        SeenTimezoneProfile = 1UL << 6,
        SeenTimezoneOffset = 1UL << 7,
        SeenTimezoneTzdef = 1UL << 8,
        SeenMessageAlerts = 1UL << 9,
        SeenContactAlerts = 1UL << 10,
        SeenAutoReplyEnabled = 1UL << 11,
        SeenAutoReplyText = 1UL << 12,
        SeenDebugLogs = 1UL << 13,
        SeenGaugeDesign = 1UL << 14,
        SeenGaugeFull = 1UL << 15,
        SeenWifiEnabled = 1UL << 16,
        SeenWifiCount = 1UL << 17,
        SeenRadioEnabled = 1UL << 18
    };
    // Radio was added as an optional record for existing TMSET7 documents.
    static constexpr uint32_t kRequiredBase = (SeenRadioEnabled - 1U);

    uint32_t seen = 0;
    uint32_t screen_timeout_ms = 30000;
    int32_t screen_brightness = 0;
    int32_t speaker_volume = 45;
    bool vibration_enabled = true;
    char locale[kLocaleBytes]{};
    char enabled_imes[kEnabledImesBytes]{};
    size_t enabled_imes_len = 0;
    int32_t timezone_profile = 0;
    int32_t timezone_offset_minutes = 0;
    uint8_t timezone_tzdef[kTimezoneTzdefBytes]{};
    size_t timezone_tzdef_len = 0;
    int32_t message_alerts = 1;
    int32_t contact_alerts = 1;
    bool auto_reply_enabled = false;
    char auto_reply_text[kAutoReplyBytes + 1]{};
    size_t auto_reply_text_len = 0;
    bool debug_logs_enabled = false;
    uint32_t gauge_design_mah = 1500;
    uint32_t gauge_full_mah = 1500;
    bool wifi_enabled = false;
    bool radio_enabled = true;
    uint8_t wifi_profile_count = 0;
    bool wifi_ssid_seen[platform::ui::wifi::kMaxSavedProfileCount]{};
    bool wifi_password_seen[platform::ui::wifi::kMaxSavedProfileCount]{};
    platform::ui::wifi::Config wifi_profiles[platform::ui::wifi::kMaxSavedProfileCount]{};

    RecordConsumeResult consume(const RecordReader& reader)
    {
        using Result = RecordConsumeResult;
#define TMS_SCALAR(document_key, method, field, bit) \
    if (!std::strcmp(reader.key(), document_key)) return mark_once(bit) && reader.method(&field) ? Result::Accepted : Result::Invalid
        TMS_SCALAR("ui.screen.timeout_ms", u32, screen_timeout_ms, SeenScreenTimeout);
        TMS_SCALAR("ui.display.brightness", i32, screen_brightness, SeenBrightness);
        TMS_SCALAR("ui.audio.speaker_volume", i32, speaker_volume, SeenSpeakerVolume);
        TMS_SCALAR("ui.haptics.vibration_enabled", boolean, vibration_enabled, SeenVibration);
        TMS_SCALAR("ui.timezone.profile", i32, timezone_profile, SeenTimezoneProfile);
        TMS_SCALAR("ui.timezone.offset_minutes", i32, timezone_offset_minutes, SeenTimezoneOffset);
        TMS_SCALAR("chat.alerts.message", i32, message_alerts, SeenMessageAlerts);
        TMS_SCALAR("chat.alerts.contact", i32, contact_alerts, SeenContactAlerts);
        TMS_SCALAR("chat.auto_reply.enabled", boolean, auto_reply_enabled, SeenAutoReplyEnabled);
        TMS_SCALAR("debug.sd_logs_enabled", boolean, debug_logs_enabled, SeenDebugLogs);
        TMS_SCALAR("power.gauge.design_mah", u32, gauge_design_mah, SeenGaugeDesign);
        TMS_SCALAR("power.gauge.full_mah", u32, gauge_full_mah, SeenGaugeFull);
        TMS_SCALAR("wifi.enabled", boolean, wifi_enabled, SeenWifiEnabled);
        TMS_SCALAR("radio.enabled", boolean, radio_enabled, SeenRadioEnabled);
#undef TMS_SCALAR
        if (!std::strcmp(reader.key(), "ui.locale"))
            return mark_once(SeenLocale) && reader.text(locale, sizeof(locale)) ? Result::Accepted : Result::Invalid;
        if (!std::strcmp(reader.key(), "ui.ime.enabled"))
            return mark_once(SeenEnabledImes) && reader.blob(reinterpret_cast<uint8_t*>(enabled_imes), sizeof(enabled_imes) - 1, &enabled_imes_len) ? Result::Accepted : Result::Invalid;
        if (!std::strcmp(reader.key(), "ui.timezone.tzdef"))
            return mark_once(SeenTimezoneTzdef) && reader.blob(timezone_tzdef, sizeof(timezone_tzdef), &timezone_tzdef_len) ? Result::Accepted : Result::Invalid;
        if (!std::strcmp(reader.key(), "chat.auto_reply.text"))
            return mark_once(SeenAutoReplyText) && reader.blob(reinterpret_cast<uint8_t*>(auto_reply_text), kAutoReplyBytes, &auto_reply_text_len) ? Result::Accepted : Result::Invalid;
        if (!std::strcmp(reader.key(), "wifi.profile_count"))
            return mark_once(SeenWifiCount) && reader.u8(&wifi_profile_count, platform::ui::wifi::kMaxSavedProfileCount) ? Result::Accepted : Result::Invalid;
        for (size_t index = 0; index < platform::ui::wifi::kMaxSavedProfileCount; ++index)
        {
            char key[48];
            std::snprintf(key, sizeof(key), "wifi.profile.%u.ssid", static_cast<unsigned>(index));
            if (!std::strcmp(reader.key(), key))
            {
                if (wifi_ssid_seen[index] || !reader.text(wifi_profiles[index].ssid, sizeof(wifi_profiles[index].ssid))) return Result::Invalid;
                wifi_ssid_seen[index] = true;
                return Result::Accepted;
            }
            std::snprintf(key, sizeof(key), "wifi.profile.%u.password", static_cast<unsigned>(index));
            if (!std::strcmp(reader.key(), key))
            {
                if (wifi_password_seen[index] || !reader.text(wifi_profiles[index].password, sizeof(wifi_profiles[index].password))) return Result::Invalid;
                wifi_password_seen[index] = true;
                return Result::Accepted;
            }
        }
        return Result::Unhandled;
    }

    bool valid_values() const
    {
        if ((screen_timeout_ms != 15000 && screen_timeout_ms != 30000 && screen_timeout_ms != 60000 && screen_timeout_ms != 300000) ||
            screen_brightness < 0 || screen_brightness > 255 || speaker_volume < 0 || speaker_volume > 100 ||
            timezone_offset_minutes < -720 || timezone_offset_minutes > 840 || !std::memchr(locale, 0, sizeof(locale)) ||
            enabled_imes_len >= sizeof(enabled_imes) || std::memchr(enabled_imes, 0, enabled_imes_len) ||
            timezone_tzdef_len > sizeof(timezone_tzdef) || auto_reply_text_len > kAutoReplyBytes ||
            std::memchr(auto_reply_text, 0, auto_reply_text_len) || message_alerts < 0 || message_alerts > 1 ||
            contact_alerts < 0 || contact_alerts > 2 || !gauge_design_mah || gauge_design_mah > 10000 ||
            !gauge_full_mah || gauge_full_mah > 10000 || wifi_profile_count > platform::ui::wifi::kMaxSavedProfileCount) return false;
        for (size_t index = 0; index < wifi_profile_count; ++index)
        {
            const auto& profile = wifi_profiles[index];
            if (!profile.ssid[0] || !std::memchr(profile.ssid, 0, sizeof(profile.ssid)) ||
                !std::memchr(profile.password, 0, sizeof(profile.password))) return false;
            for (size_t prior = 0; prior < index; ++prior)
                if (!std::strcmp(profile.ssid, wifi_profiles[prior].ssid)) return false;
        }
        return true;
    }
    bool valid() const
    {
        if ((seen & kRequiredBase) != kRequiredBase || !valid_values()) return false;
        for (size_t index = 0; index < platform::ui::wifi::kMaxSavedProfileCount; ++index)
        {
            if (index < wifi_profile_count)
            {
                if (!wifi_ssid_seen[index] || !wifi_password_seen[index]) return false;
            }
            else if (wifi_ssid_seen[index] || wifi_password_seen[index]) return false;
        }
        return true;
    }
    bool write(RecordWriter& writer) const
    {
        if (!valid_values()) return false;
        bool ok = writer.u32("ui.screen.timeout_ms", screen_timeout_ms) && writer.i32("ui.display.brightness", screen_brightness) &&
                  writer.i32("ui.audio.speaker_volume", speaker_volume) && writer.boolean("radio.enabled", radio_enabled) &&
                  writer.boolean("ui.haptics.vibration_enabled", vibration_enabled) && writer.text("ui.locale", locale) &&
                  writer.blob("ui.ime.enabled", reinterpret_cast<const uint8_t*>(enabled_imes), enabled_imes_len) &&
                  writer.i32("ui.timezone.profile", timezone_profile) && writer.i32("ui.timezone.offset_minutes", timezone_offset_minutes) &&
                  writer.blob("ui.timezone.tzdef", timezone_tzdef, timezone_tzdef_len) && writer.i32("chat.alerts.message", message_alerts) &&
                  writer.i32("chat.alerts.contact", contact_alerts) && writer.boolean("chat.auto_reply.enabled", auto_reply_enabled) &&
                  writer.blob("chat.auto_reply.text", reinterpret_cast<const uint8_t*>(auto_reply_text), auto_reply_text_len) &&
                  writer.boolean("debug.sd_logs_enabled", debug_logs_enabled) && writer.u32("power.gauge.design_mah", gauge_design_mah) &&
                  writer.u32("power.gauge.full_mah", gauge_full_mah) && writer.boolean("wifi.enabled", wifi_enabled) &&
                  writer.u8("wifi.profile_count", wifi_profile_count);
        for (size_t index = 0; ok && index < wifi_profile_count; ++index)
        {
            char key[48];
            std::snprintf(key, sizeof(key), "wifi.profile.%u.ssid", static_cast<unsigned>(index));
            ok = writer.text(key, wifi_profiles[index].ssid);
            std::snprintf(key, sizeof(key), "wifi.profile.%u.password", static_cast<unsigned>(index));
            ok = ok && writer.text(key, wifi_profiles[index].password);
        }
        return ok;
    }

  private:
    bool mark_once(uint32_t bit)
    {
        if (seen & bit) return false;
        seen |= bit;
        return true;
    }
};
static_assert(sizeof(SettingsRecords) <= 3 * 1024, "Settings records require bounded caller-owned staging");
} // namespace app::tms
