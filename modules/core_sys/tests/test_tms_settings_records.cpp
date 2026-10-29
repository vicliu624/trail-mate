#include "app/tms_settings_records.h"
#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace tms = app::tms;
namespace
{
bool append(void* context, const char* data, size_t size)
{
    static_cast<std::string*>(context)->append(data, size);
    return true;
}
// This exercises the core + base extension only, not a complete portable
// document (network and BLE compatibility extensions have their own owners).
std::string encode(tms::SettingsRecords& state)
{
    auto config = std::make_unique<app::AppConfig>();
    tms::LineScratch scratch;
    std::string document;
    assert(tms::writeDocument(
        *config, tms::DocumentKind::Working, {&document, append}, scratch, nullptr,
        [](void* context, tms::RecordWriter& writer)
        { return static_cast<tms::SettingsRecords*>(context)->write(writer); },
        &state));
    return document;
}
bool decode(const std::string& document, tms::SettingsRecords& state)
{
    tms::Decoder decoder(
        nullptr, tms::DocumentKind::Working,
        [](void* context, const tms::RecordReader& reader)
        { return static_cast<tms::SettingsRecords*>(context)->consume(reader); },
        &state,
        [](void* context, bool, uint16_t)
        { return static_cast<tms::SettingsRecords*>(context)->valid(); });
    size_t start = 0;
    while (start < document.size())
    {
        const auto end = document.find('\n', start);
        const auto length = (end == std::string::npos ? document.size() : end) - start;
        if (length >= tms::kMaxLineBytes) return false;
        tms::LineScratch scratch{};
        std::memcpy(scratch.bytes, document.data() + start, length);
        if (!decoder.consumeLine(scratch.bytes)) return false;
        start += length + 1;
    }
    return decoder.finish();
}
std::string replace_record(std::string document, const char* key, const std::string& value)
{
    const auto start = document.find(std::string(key) + '=');
    assert(start != std::string::npos);
    const auto end = document.find('\n', start);
    assert(end != std::string::npos);
    document.replace(start, end - start + 1, value.empty() ? "" : std::string(key) + '=' + value + '\n');
    return document;
}
bool accepted(const std::string& document)
{
    auto parsed = std::make_unique<tms::SettingsRecords>();
    return decode(document, *parsed);
}
} // namespace

int main()
{
    auto source = std::make_unique<tms::SettingsRecords>();
    source->screen_timeout_ms = 60000;
    source->screen_brightness = 207;
    source->speaker_volume = 75;
    source->radio_enabled = false;
    source->vibration_enabled = false;
    std::strcpy(source->locale, "zh-Hans");
    std::strcpy(source->enabled_imes, "pinyin,latin");
    source->enabled_imes_len = std::strlen(source->enabled_imes);
    source->timezone_profile = 8;
    source->timezone_offset_minutes = 330;
    source->timezone_tzdef[0] = 1;
    source->timezone_tzdef[1] = 0;
    source->timezone_tzdef[2] = 255;
    source->timezone_tzdef_len = 3;
    source->contact_alerts = 2;
    source->auto_reply_enabled = true;
    std::strcpy(source->auto_reply_text, "Line one\nLine two");
    source->auto_reply_text_len = std::strlen(source->auto_reply_text);
    source->debug_logs_enabled = true;
    source->gauge_design_mah = 2000;
    source->gauge_full_mah = 1900;
    source->wifi_enabled = true;
    source->wifi_profile_count = 2;
    std::strcpy(source->wifi_profiles[0].ssid, "AP % spaces");
    std::strcpy(source->wifi_profiles[0].password, "secret");
    std::strcpy(source->wifi_profiles[1].ssid, "Open AP");
    const auto document = encode(*source);
    auto restored = std::make_unique<tms::SettingsRecords>();
    assert(decode(document, *restored) && restored->valid());
    assert(restored->screen_brightness == 207 && restored->speaker_volume == 75 && !restored->radio_enabled);
    assert(!std::strcmp(restored->locale, "zh-Hans") && !std::strcmp(restored->enabled_imes, "pinyin,latin"));
    assert(restored->timezone_tzdef_len == 3 && restored->timezone_tzdef[1] == 0 && restored->timezone_tzdef[2] == 255);
    assert(restored->wifi_profile_count == 2 && !restored->wifi_profiles[1].password[0]);
    assert(encode(*restored) == document);
    for (const auto* key : {"ui.screen.timeout_ms", "ui.display.brightness", "ui.audio.speaker_volume", "ui.haptics.vibration_enabled",
                            "ui.locale", "ui.ime.enabled", "ui.timezone.profile", "ui.timezone.offset_minutes", "ui.timezone.tzdef",
                            "chat.alerts.message", "chat.alerts.contact", "chat.auto_reply.enabled", "chat.auto_reply.text", "debug.sd_logs_enabled",
                            "power.gauge.design_mah", "power.gauge.full_mah", "wifi.enabled", "wifi.profile_count",
                            "wifi.profile.0.ssid", "wifi.profile.0.password", "wifi.profile.1.password"})
        assert(!accepted(replace_record(document, key, "")));
    assert(accepted(replace_record(document, "radio.enabled", ""))); // Existing optional-radio migration policy.
    for (const auto& entry : {std::pair{"ui.screen.timeout_ms", "u32:10000"}, {"ui.screen.timeout_ms", "i32:30000"}, {"ui.display.brightness", "i32:256"}, {"ui.audio.speaker_volume", "i32:101"}, {"ui.timezone.offset_minutes", "i32:841"}, {"ui.timezone.offset_minutes", "i32:-721"}, {"chat.alerts.message", "i32:2"}, {"chat.alerts.contact", "i32:3"}, {"power.gauge.design_mah", "u32:0"}, {"power.gauge.full_mah", "u32:10001"}, {"wifi.profile_count", "u8:11"}, {"chat.auto_reply.text", "b64:QQBC"}, {"ui.ime.enabled", "b64:QQBC"}, {"wifi.profile.1.ssid", "str:AP%20%25%20spaces"}})
        assert(!accepted(replace_record(document, entry.first, entry.second)));
    auto duplicate = document;
    duplicate.insert(duplicate.rfind("END\n"), "ui.locale=str:en\n");
    assert(!accepted(duplicate));
    auto extra = document;
    extra.insert(extra.rfind("END\n"), "wifi.profile.2.ssid=str:extra\n");
    assert(!accepted(extra));
    std::memset(source->wifi_profiles[0].ssid, 'x', sizeof(source->wifi_profiles[0].ssid));
    assert(!source->valid_values());
    return 0;
}
