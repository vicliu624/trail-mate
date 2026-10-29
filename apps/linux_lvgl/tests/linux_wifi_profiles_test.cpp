#include "app/linux_app_services.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/settings_transaction.h"
#include "platform/ui/settings_reset_runtime.h"
#include "platform/ui/wifi_profile_snapshot.h"
#include "sys/event_bus.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace wifi = platform::ui::wifi;
namespace store = platform::ui::settings_store;
namespace native = platform::linux_runtime;
struct Snapshot
{
    std::array<wifi::Config, wifi::kMaxSavedProfileCount> profiles{};
    bool enabled = false;
    size_t count = 0;
};
wifi::Config config(const char* ssid, const char* password, bool enabled = true)
{
    wifi::Config result;
    result.enabled = enabled;
    std::snprintf(result.ssid, sizeof(result.ssid), "%s", ssid);
    std::snprintf(result.password, sizeof(result.password), "%s", password);
    return result;
}
Snapshot snapshot()
{
    Snapshot result;
    assert(wifi::read_saved_profiles_into(&result.enabled, &result.count, result.profiles.data(), result.profiles.size()));
    return result;
}
struct Database
{
    sqlite3* db = nullptr;
    Database()
    {
        assert(sqlite3_open(native::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
        assert(sqlite3_busy_timeout(db, 5000) == SQLITE_OK);
    }
    ~Database() { sqlite3_close(db); }
    void sql(const char* text) { assert(sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK); }
};
int main(int argc, char* argv[])
{
    if (argc == 3 && !std::strcmp(argv[1], "--verify"))
    {
        const std::filesystem::path root(argv[2]);
        setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
        setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
        setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
        const auto restored = snapshot();
        assert(restored.enabled && restored.count == wifi::kMaxSavedProfileCount);
        for (size_t i = 0; i < restored.count; ++i)
        {
            wifi::Config selected;
            assert(wifi::find_saved_config(("Network-" + std::to_string(i)).c_str(), selected));
            assert(selected.enabled && std::string(selected.password) == std::string(64, 'x'));
        }
        std::puts("Fresh process verified all ten saved Wi-Fi profiles");
        return 0;
    }
    char pattern[] = "/tmp/trailmate-wifi-profiles-XXXXXX";
    const auto* temporary = mkdtemp(pattern);
    assert(temporary);
    const std::filesystem::path root(temporary);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    auto fresh = snapshot();
    assert(fresh.enabled && fresh.count == 0);
    assert(store::put_string("settings", "wifi_ssid", "Legacy"));
    assert(store::put_string("settings", "wifi_password", "old-password"));
    wifi::Config selected;
    assert(wifi::load_config(selected) && !std::strcmp(selected.ssid, "Legacy"));
    assert(snapshot().count == 1);
    assert(wifi::save_config(config("Second", "second-password")));
    assert(wifi::save_config(config("Third", "third-password", false)));
    auto saved = snapshot();
    assert(saved.count == 3 && !saved.enabled && !std::strcmp(saved.profiles[0].ssid, "Third"));
    assert(wifi::find_saved_config("Legacy", selected) && !selected.enabled && !std::strcmp(selected.password, "old-password"));
    assert(wifi::save_config(config("Legacy", "updated")));
    assert(snapshot().count == 3 && wifi::load_config(selected) && !std::strcmp(selected.password, "updated"));
    size_t visits = 0, count = 0;
    bool enabled = false;
    assert(wifi::visit_saved_profiles(
        &enabled, &count,
        [](void* context, size_t index, const wifi::Config& entry)
        {
            assert(index == (*static_cast<size_t*>(context))++ && entry.ssid[0]);
            // A visitor can use the ordinary store after snapshot capture.
            (void)store::get_bool("settings", "wifi_enabled", false);
            return true;
        },
        &visits));
    assert(enabled && visits == count && count == 3);
    const std::string unicode_ssid = std::string(29, 'A') + "\xe4\xb8\xad";
    assert(wifi::save_config(config(unicode_ssid.c_str(), std::string(64, 'x').c_str())));
    assert(wifi::find_saved_config(unicode_ssid.c_str(), selected) && std::strlen(selected.ssid) == 32 && std::strlen(selected.password) == 64);
    auto invalid_password = selected;
    std::memset(invalid_password.password, 'x', sizeof(invalid_password.password));
    assert(!wifi::save_config(invalid_password));

    std::array<wifi::Config, wifi::kMaxSavedProfileCount> full;
    for (size_t i = 0; i < full.size(); ++i)
    {
        full[i] = config(("Network-" + std::to_string(i)).c_str(), std::string(64, 'x').c_str());
    }
    assert(wifi::replace_saved_profiles(true, full.data(), full.size()));
    assert(snapshot().count == full.size());
    assert(wifi::save_config(config("Newest", "")));
    assert(!wifi::find_saved_config("Network-9", selected) && wifi::find_saved_config("Network-0", selected));
    auto duplicate = full;
    duplicate[1] = duplicate[0];
    assert(!wifi::replace_saved_profiles(true, duplicate.data(), duplicate.size()));
    auto invalid = config("valid", "valid");
    std::memset(invalid.ssid, 'x', sizeof(invalid.ssid));
    assert(!wifi::save_config(invalid));

    {
        Database db;
        db.sql("CREATE TRIGGER reject_wifi BEFORE INSERT ON settings WHEN NEW.key='wifi_password_1' BEGIN SELECT RAISE(ABORT,'injected'); END;");
        const auto before = snapshot();
        assert(!wifi::replace_saved_profiles(false, full.data(), 2));
        const auto after = snapshot();
        assert(after.enabled == before.enabled && after.count == before.count);
        for (size_t i = 0; i < before.count; ++i)
            assert(!std::strcmp(before.profiles[i].ssid, after.profiles[i].ssid) && !std::strcmp(before.profiles[i].password, after.profiles[i].password));
        assert(wifi::load_config(selected) && !std::strcmp(selected.ssid, "Newest"));
        db.sql("DROP TRIGGER reject_wifi;");
    }
    assert(wifi::replace_saved_profiles(true, full.data(), 1));
    {
        // Commit a competing raw SQLite write after the first snapshot read.
        // The production read view must continue seeing the complete old set.
        Database writer;
        struct Reader
        {
            const native::SettingsReadView& view;
            Database& writer;
            store::ScalarReadResult read_bool_into(const char* ns, const char* key, bool* out) { return view.read_bool_into(ns, key, out); }
            store::ScalarReadResult read_int_into(const char* ns, const char* key, int32_t* out)
            {
                const auto result = view.read_int_into(ns, key, out);
                writer.sql("BEGIN IMMEDIATE; UPDATE settings SET payload=X'436F6D706574696E67' WHERE namespace='settings' AND key IN('wifi_ssid_0','wifi_ssid'); COMMIT;");
                return result;
            }
            store::StringReadResult read_string_into(const char* ns, const char* key, char* out, size_t cap, size_t* length) { return view.read_string_into(ns, key, out, cap, length); }
        };
        struct Context
        {
            Database& writer;
            Snapshot& state;
        };
        Snapshot old;
        Context context{writer, old};
        assert(native::read_settings_snapshot([](void* pointer, const native::SettingsReadView& view)
                                              {
            auto& context = *static_cast<Context*>(pointer);
            Reader reader{view, context.writer};
            return wifi::profile_snapshot::read_persisted_wifi_profiles(reader, &context.state.enabled, &context.state.count,
                context.state.profiles.data(), context.state.profiles.size(), true); },
                                              &context));
        assert(old.count == 1 && !std::strcmp(old.profiles[0].ssid, "Network-0"));
        assert(!std::strcmp(snapshot().profiles[0].ssid, "Competing"));
    }
    assert(wifi::replace_saved_profiles(false, nullptr, 0));
    assert(snapshot().count == 0 && !snapshot().enabled);
    assert(wifi::load_config(selected) && !selected.ssid[0]);
    std::atomic<bool> go{false};
    std::array<std::thread, wifi::kMaxSavedProfileCount> threads;
    for (size_t i = 0; i < threads.size(); ++i)
        threads[i] = std::thread([&, i]
                                 {
            while (!go.load()) std::this_thread::yield();
            assert(wifi::save_config(full[i])); });
    go = true;
    for (auto& thread : threads) thread.join();
    assert(snapshot().count == threads.size());
    for (const auto& entry : full) assert(wifi::find_saved_config(entry.ssid, selected));
    const auto verifier = fork();
    assert(verifier >= 0);
    if (!verifier)
    {
        execl("/proc/self/exe", argv[0], "--verify", root.c_str(), nullptr);
        _exit(127);
    }
    int verified = 0;
    assert(waitpid(verifier, &verified, 0) == verifier && WIFEXITED(verified) && WEXITSTATUS(verified) == 0);
    store::put_uint("settings", "wifi_prof_count", 2); // Wrong persistent kind.
    Snapshot bad;
    bad.enabled = true;
    bad.count = 99;
    assert(!wifi::read_saved_profiles_into(&bad.enabled, &bad.count, bad.profiles.data(), bad.profiles.size()));
    assert(!bad.enabled && !bad.count);
    for (const auto& entry : bad.profiles) assert(!entry.ssid[0] && !entry.password[0]);
    assert(!wifi::save_config(config("Do-not-overwrite", "unchanged")));
    assert(wifi::replace_saved_profiles(true, full.data(), 1));
    {
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = native::LinuxRuntimeMode::SimulatorDemo;
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize() && platform::ui::settings_reset::begin());
        assert(!wifi::save_config(full[0]) && !wifi::replace_saved_profiles(true, full.data(), 1));
        assert(snapshot().count == 0);
        services.shutdown();
    }
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event, 0)) delete event;
    std::filesystem::remove_all(root);
    std::puts("Native Wi-Fi: legacy recovery, ten profiles, lookup, MRU, atomic rollback, consistent snapshot, concurrent saves and reset fence passed");
}
