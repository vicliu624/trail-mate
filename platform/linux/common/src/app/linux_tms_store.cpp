#include "app/linux_tms_store.h"
#include "app/linux_app_config_blob.h"
#include "chat/domain/reticulum_network_policy.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/ui/screen_power_state_machine.h"
#include "platform/ui/wifi_profile_snapshot.h"
#include <cstring>

namespace trailmate::linux_app
{
namespace
{
namespace native = platform::linux_runtime;
namespace store = platform::ui::settings_store;
namespace tms = app::tms;
constexpr const char* kCompatibilityKey = "tms_compatibility_v1";
using native::SettingsMutation;

bool read_snapshot(void* context, const native::SettingsReadView& reader)
{
    auto& staged = *static_cast<LinuxTmsDocument*>(context);
    auto blob = std::make_unique<LinuxAppConfigBlob>();
    size_t length = 0;
    auto result = reader.read_blob_into(native::settings_storage::kAppNamespace, native::settings_storage::kAppConfigKey,
                                        blob.get(), sizeof(*blob), &length);
    if (result == store::BlobReadResult::Missing)
    {
        staged.config.ble_enabled = false;
    }
    else
    {
        if (result != store::BlobReadResult::Ok || length != sizeof(*blob) ||
            blob->magic != LinuxAppConfigBlob::kMagic || blob->version != LinuxAppConfigBlob::kVersion) return false;
        staged.config = blob->config;
    }
    auto bytes = std::make_unique<std::array<char, tms::kMaxDocumentBytes>>();
    result = reader.read_blob_into(native::settings_storage::kAppNamespace, kCompatibilityKey,
                                   bytes->data(), bytes->size(), &length);
    if (result == store::BlobReadResult::Missing)
    {
        staged.compatibility.mt_ble.enabled = false;
        staged.compatibility.mt_ble.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
        phone::meshtastic::config_bridge::initDefaultModuleConfig(&staged.compatibility.module, 0);
    }
    else
    {
        if (result != store::BlobReadResult::Ok) return false;
        auto preserved = LinuxTmsDocument::parse(bytes->data(), length, tms::DocumentKind::Working);
        if (!preserved) return false;
        staged.compatibility = preserved->compatibility;
        staged.companion = preserved->companion;
    }
    result = reader.read_blob_into(native::settings_storage::kNetworkNamespace, "network_tms_v1",
                                   bytes->data(), bytes->size(), &length);
    if (result == store::BlobReadResult::Missing)
    {
        if (!chat::reticulum::network_config_policy::build_defaults(staged.config.reticulumConfig(), &staged.network)) return false;
    }
    else if (result != store::BlobReadResult::Ok ||
             !native::LinuxReticulumNetworkStore::decode(bytes->data(), length, staged.network)) return false;

    auto& settings = staged.settings;
    settings.screen_timeout_ms = platform::ui::screen_power::StateMachine::kDefaultTimeoutMs;
    const auto scalar = [](auto read, auto& field)
    {
        const auto fallback = field;
        const auto status = read(&field);
        if (status == store::ScalarReadResult::Missing)
        {
            field = fallback;
            return true;
        }
        return status == store::ScalarReadResult::Ok;
    };
#define READ_SCALAR(method, ns, key, field) \
    if (!scalar([&](auto* out) { return reader.method(ns, key, out); }, settings.field)) return false
    READ_SCALAR(read_uint_into, "settings", "screen_timeout", screen_timeout_ms);
    READ_SCALAR(read_int_into, "settings", "screen_brightness", screen_brightness);
    READ_SCALAR(read_int_into, "settings", "speaker_volume", speaker_volume);
    READ_SCALAR(read_bool_into, "settings", "vibration_enabled", vibration_enabled);
    READ_SCALAR(read_bool_into, "settings", "radio_enabled", radio_enabled);
    READ_SCALAR(read_int_into, "settings", "timezone_profile", timezone_profile);
    READ_SCALAR(read_int_into, "settings", "timezone_offset", timezone_offset_minutes);
    READ_SCALAR(read_int_into, "settings", "chat_message_alerts", message_alerts);
    READ_SCALAR(read_int_into, "settings", "chat_contact_alerts", contact_alerts);
    READ_SCALAR(read_bool_into, "settings", platform::ui::auto_reply::kEnabledKey, auto_reply_enabled);
    READ_SCALAR(read_bool_into, "settings", "adv_debug", debug_logs_enabled);
    READ_SCALAR(read_uint_into, "power", "gauge_design_mah", gauge_design_mah);
    READ_SCALAR(read_uint_into, "power", "gauge_full_mah", gauge_full_mah);
#undef READ_SCALAR
    const auto text = [&](const char* key, char* out, size_t capacity, size_t& size)
    {
        const auto status = reader.read_string_into("settings", key, out, capacity, &size);
        return status == store::StringReadResult::Ok || status == store::StringReadResult::Missing;
    };
    size_t locale_size = 0;
    if (!text("display_locale", settings.locale, sizeof(settings.locale), locale_size) ||
        !text("enabled_imes", settings.enabled_imes, sizeof(settings.enabled_imes), settings.enabled_imes_len) ||
        !text(platform::ui::auto_reply::kTextKey, settings.auto_reply_text, sizeof(settings.auto_reply_text), settings.auto_reply_text_len)) return false;
    result = reader.read_blob_into("settings", "timezone_tzdef", settings.timezone_tzdef,
                                   sizeof(settings.timezone_tzdef), &settings.timezone_tzdef_len);
    if (result != store::BlobReadResult::Ok && result != store::BlobReadResult::Missing) return false;
    size_t count = 0;
    if (!platform::ui::wifi::profile_snapshot::read_persisted_wifi_profiles(reader, &settings.wifi_enabled, &count,
                                                                            settings.wifi_profiles, platform::ui::wifi::kMaxSavedProfileCount, true)) return false;
    settings.wifi_profile_count = static_cast<uint8_t>(count);
    return settings.valid_values();
}
} // namespace

bool export_linux_tms_snapshot(tms::DocumentKind kind, std::string& output)
{
    auto staged = std::make_unique<LinuxTmsDocument>();
    return native::read_settings_snapshot(read_snapshot, staged.get()) && staged->write(kind, output);
}
bool export_linux_tms_snapshot(const native::SettingsReadView& reader, tms::DocumentKind kind, std::string& output)
{
    auto staged = std::make_unique<LinuxTmsDocument>();
    return read_snapshot(staged.get(), reader) && staged->write(kind, output);
}

std::unique_ptr<LinuxTmsImport> LinuxTmsImport::prepare(const char* bytes, size_t length, tms::DocumentKind kind)
{
    auto staged = LinuxTmsDocument::parse(bytes, length, kind);
    if (!staged) return nullptr;
    auto plan = std::unique_ptr<LinuxTmsImport>(new LinuxTmsImport);
    // Linux keeps the phone protocol disabled, independently of imported data.
    staged->config.ble_enabled = false;
    auto blob = std::make_unique<LinuxAppConfigBlob>();
    blob->config = staged->config;
    auto& app_changes = plan->mutations_[0];
    app_changes.push_back({native::settings_storage::kAppConfigKey, 'x',
                           std::string(reinterpret_cast<const char*>(blob.get()), sizeof(*blob))});
    std::string preserved, network;
    if (!staged->write(tms::DocumentKind::Working, preserved) ||
        !native::LinuxReticulumNetworkStore::encode(staged->network, staged->config, network)) return nullptr;
    app_changes.push_back({kCompatibilityKey, 'x', std::move(preserved)});
    plan->mutations_[1].push_back({"network_tms_v1", 'x', std::move(network)});
    const auto& settings = staged->settings;
    auto& changes = plan->mutations_[2];
#define NUMBER(key, kind, field) changes.push_back({key, kind, std::to_string(settings.field)})
    NUMBER("screen_timeout", 'u', screen_timeout_ms);
    NUMBER("screen_brightness", 'i', screen_brightness);
    NUMBER("speaker_volume", 'i', speaker_volume);
    NUMBER("vibration_enabled", 'b', vibration_enabled);
    if (settings.seen & tms::SettingsRecords::SeenRadioEnabled) NUMBER("radio_enabled", 'b', radio_enabled);
    NUMBER("timezone_profile", 'i', timezone_profile);
    NUMBER("timezone_offset", 'i', timezone_offset_minutes);
    NUMBER("chat_message_alerts", 'i', message_alerts);
    NUMBER("chat_contact_alerts", 'i', contact_alerts);
    NUMBER(platform::ui::auto_reply::kEnabledKey, 'b', auto_reply_enabled);
    NUMBER("adv_debug", 'b', debug_logs_enabled);
    NUMBER("wifi_enabled", 'b', wifi_enabled);
    NUMBER("wifi_prof_count", 'i', wifi_profile_count);
#undef NUMBER
    changes.push_back({"display_locale", 's', settings.locale});
    changes.push_back({"enabled_imes", 's', std::string(settings.enabled_imes, settings.enabled_imes_len)});
    changes.push_back({platform::ui::auto_reply::kTextKey, 's', std::string(settings.auto_reply_text, settings.auto_reply_text_len)});
    changes.push_back({"timezone_tzdef", 'x', std::string(reinterpret_cast<const char*>(settings.timezone_tzdef), settings.timezone_tzdef_len)});
    for (size_t i = 0; i < platform::ui::wifi::kMaxSavedProfileCount; ++i)
    {
        const auto suffix = std::to_string(i);
        const bool erase = i >= settings.wifi_profile_count;
        changes.push_back({"wifi_ssid_" + suffix, 's', erase ? "" : settings.wifi_profiles[i].ssid, erase});
        changes.push_back({"wifi_password_" + suffix, 's', erase ? "" : settings.wifi_profiles[i].password, erase});
        changes.push_back({"wifi_pass_" + suffix, 's', "", true});
    }
    changes.push_back({"wifi_ssid", 's', settings.wifi_profile_count ? settings.wifi_profiles[0].ssid : ""});
    changes.push_back({"wifi_password", 's', settings.wifi_profile_count ? settings.wifi_profiles[0].password : ""});
    plan->mutations_[3] = {{"gauge_design_mah", 'u', std::to_string(settings.gauge_design_mah)},
                           {"gauge_full_mah", 'u', std::to_string(settings.gauge_full_mah)}};
    return plan;
}

bool LinuxTmsImport::commit()
{
    const char* namespaces[] = {native::settings_storage::kAppNamespace, native::settings_storage::kNetworkNamespace, "settings", "power"};
    std::array<native::SettingsNamespaceBatch, 4> batches;
    for (size_t i = 0; i < batches.size(); ++i) batches[i] = {namespaces[i], mutations_[i].data(), mutations_[i].size()};
    return native::commit_settings_import(batches.data(), batches.size());
}
LinuxTmsImport::CommitResult LinuxTmsImport::commit(const TmsCommitOptions& options)
{
    if (options.receipt.empty() || options.receipt.size() > tms::kMaxDocumentBytes + 111) return CommitResult::StorageError;
    const char* namespaces[] = {native::settings_storage::kAppNamespace, native::settings_storage::kNetworkNamespace, "settings", "power"};
    std::array<native::SettingsNamespaceBatch, 5> batches;
    for (size_t i = 0; i < mutations_.size(); ++i) batches[i] = {namespaces[i], mutations_[i].data(), mutations_[i].size()};
    const native::SettingsMutation receipt{kTmsTransferKey, 'x', options.receipt};
    batches.back() = {kTmsTransferNamespace, &receipt, 1};
    struct Check
    {
        const TmsCommitOptions& options;
        bool compared = false, matched = false;
    } check{options};
    const bool committed = native::commit_settings_import_if(
        batches.data(), batches.size(),
        [](void* context, const native::SettingsReadView& view)
        {
            auto& check = *static_cast<Check*>(context);
            std::string document;
            std::array<uint8_t, 32> hash{};
            native::LinuxGeocachingCrypto crypto;
            if (!export_linux_tms_snapshot(view, tms::DocumentKind::Working, document) ||
                !crypto.sha256({reinterpret_cast<const uint8_t*>(document.data()), document.size()}, hash.data())) return false;
            check.compared = true;
            check.matched = hash == check.options.expected_base_hash;
            return check.matched;
        },
        &check);
    if (committed) return CommitResult::Committed;
    return check.compared && !check.matched ? CommitResult::Conflict : CommitResult::StorageError;
}
} // namespace trailmate::linux_app
