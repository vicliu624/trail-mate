#include "app/linux_tms_store.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/settings_reset_storage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace tms = app::tms;
namespace store = platform::ui::settings_store;
using namespace trailmate::linux_app;

static void verify_import()
{
    std::string document;
    assert(export_linux_tms_snapshot(tms::DocumentKind::Backup, document));
    auto decoded = LinuxTmsDocument::parse(document.data(), document.size(), tms::DocumentKind::Backup);
    assert(decoded);
    assert(!std::strcmp(decoded->config.node_name, "imported node"));
    assert(!decoded->config.ble_enabled);
    assert(decoded->settings.screen_timeout_ms == 15000);
    assert(decoded->settings.speaker_volume == 66);
    assert(decoded->settings.auto_reply_enabled);
    assert(!std::strcmp(decoded->settings.auto_reply_text, "ready"));
    assert(decoded->settings.timezone_tzdef_len == 3 && decoded->settings.timezone_tzdef[1] == 0);
    assert(decoded->settings.wifi_profile_count == 10);
    assert(!std::strcmp(decoded->settings.wifi_profiles[9].ssid, "wifi-9"));
    assert(!std::strcmp(decoded->settings.wifi_profiles[9].password, "pass-9"));
    assert(decoded->network.interface_count == 0);
    assert(decoded->compatibility.module.serial.enabled);
    assert(decoded->companion.seen == tms::c6::kComplete);
    assert(decoded->settings.gauge_design_mah == 2200);
}

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--reload"))
    {
        assert(!native::settings_import_committed());
        verify_import();
        store::put_int("settings", "speaker_volume", 66);
        assert(store::get_int("settings", "speaker_volume", 0) == 66);
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-tms-store-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    std::string original;
    assert(export_linux_tms_snapshot(tms::DocumentKind::Working, original));
    auto source = LinuxTmsDocument::parse(original.data(), original.size(), tms::DocumentKind::Working);
    assert(source);
    assert(source->settings.screen_timeout_ms == 60000);
    std::strcpy(source->config.node_name, "imported node");
    source->config.ble_enabled = true;
    source->settings.screen_timeout_ms = 15000;
    source->settings.speaker_volume = 66;
    source->settings.auto_reply_enabled = true;
    std::strcpy(source->settings.auto_reply_text, "ready");
    source->settings.auto_reply_text_len = 5;
    source->settings.timezone_tzdef_len = 3;
    source->settings.timezone_tzdef[0] = 1;
    source->settings.timezone_tzdef[1] = 0;
    source->settings.timezone_tzdef[2] = 2;
    source->settings.wifi_profile_count = 10;
    for (unsigned i = 0; i < 10; ++i)
    {
        std::snprintf(source->settings.wifi_profiles[i].ssid, sizeof(source->settings.wifi_profiles[i].ssid), "wifi-%u", i);
        std::snprintf(source->settings.wifi_profiles[i].password, sizeof(source->settings.wifi_profiles[i].password), "pass-%u", i);
    }
    source->network = chat::reticulum::ReticulumNetworkConfig{};
    source->compatibility.module.serial.enabled = true;
    source->companion.seen = tms::c6::kComplete;
    source->settings.gauge_design_mah = 2200;
    std::string incoming;
    assert(source->write(tms::DocumentKind::Working, incoming));
    auto plan = LinuxTmsImport::prepare(incoming.data(), incoming.size(), tms::DocumentKind::Working);
    assert(plan && !native::settings_import_committed());
    assert(!LinuxTmsImport::prepare("broken", 6, tms::DocumentKind::Working));
    std::string after_prepare;
    assert(export_linux_tms_snapshot(tms::DocumentKind::Working, after_prepare) && after_prepare == original);

    sqlite3* db = nullptr;
    assert(sqlite3_open(native::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "CREATE TRIGGER reject_import BEFORE INSERT ON settings WHEN NEW.namespace='power' BEGIN SELECT RAISE(ABORT,'injected'); END;", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(!plan->commit() && !native::settings_import_committed());
    assert(export_linux_tms_snapshot(tms::DocumentKind::Working, after_prepare) && after_prepare == original);
    store::put_int("settings", "speaker_volume", 12);
    assert(store::get_int("settings", "speaker_volume", 0) == 12);
    assert(sqlite3_exec(db, "DROP TRIGGER reject_import;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    assert(plan->commit() && native::settings_import_committed());
    assert(!native::settings_storage::factory_reset_committed());
    assert(!plan->commit());
    verify_import();
    store::put_int("settings", "speaker_volume", 9);
    assert(store::get_int("settings", "speaker_volume", 0) == 66);
    assert(!store::put_string(native::settings_storage::kNetworkNamespace, "network_tms_v1", "stale"));
    const native::SettingsMutation change{"unrelated", 'i', "1"};
    const native::SettingsNamespaceBatch mixed[] = {{"other", &change, 1}, {"settings", &change, 1}};
    assert(!native::write_settings_transaction(mixed, 2));
    assert(store::get_int("other", "unrelated", 0) == 0);
    store::put_int("other", "allowed", 1);
    assert(store::get_int("other", "allowed", 0) == 1);

    const auto child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        execl(argv[0], argv[0], "--reload", static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::filesystem::remove_all(root);
    std::puts("Native TMS storage: complete snapshot, atomic rollback, ten Wi-Fi profiles, compatibility retention, import fence and fresh-exec persistence passed");
}
