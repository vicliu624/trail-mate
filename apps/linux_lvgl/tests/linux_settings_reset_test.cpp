#include "app/linux_app_services.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/settings_reset_storage.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/settings_reset_policy.h"
#include "platform/ui/settings_reset_runtime.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace storage = platform::ui::settings_store;
namespace reset_storage = platform::linux_runtime::settings_storage;
namespace reset = platform::ui::settings_reset;
using Services = trailmate::linux_app::LinuxAppServices;
using NetworkStore = platform::linux_runtime::LinuxReticulumNetworkStore;

std::vector<uint8_t> appBlob()
{
    std::vector<uint8_t> bytes;
    assert(storage::get_blob(reset_storage::kAppNamespace, reset_storage::kAppConfigKey, bytes));
    return bytes;
}
bool contains(const std::vector<uint8_t>& bytes, const std::string& text)
{
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}
void persistName(Services& services, const char* name)
{
    auto edit = services.beginConfigEdit();
    std::snprintf(edit.config().node_name, sizeof(edit.config().node_name), "%s", name);
    edit.commit(app::AppConfigChangeSet::identity());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    for (;;)
    {
        services.updateCoreServices();
        std::vector<uint8_t> bytes;
        if (storage::get_blob(reset_storage::kAppNamespace, reset_storage::kAppConfigKey, bytes) && contains(bytes, name)) return;
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
struct Database
{
    sqlite3* db = nullptr;
    Database()
    {
        assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
        assert(sqlite3_busy_timeout(db, 5000) == SQLITE_OK);
    }
    ~Database() { sqlite3_close(db); }
    void sql(const char* text) { assert(sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK); }
    void checkPreserved()
    {
        sqlite3_stmt* statement = nullptr;
        assert(sqlite3_prepare_v2(db, "SELECT value FROM reset_preserved WHERE id=1;", -1, &statement, nullptr) == SQLITE_OK);
        assert(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 12345);
        sqlite3_finalize(statement);
    }
};
int child(char* const argv[], const std::filesystem::path& root)
{
    alarm(45);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    const std::string phase = std::getenv("TRAIL_MATE_RESET_TEST_PHASE");
    platform::linux_runtime::ApplicationLifecycleOwner lifecycle;
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
    options.default_node_name = "Reset fixture default";
    Services services(options);
    assert(services.initialize());
    if (phase == "reset")
    {
        assert(!reset_storage::factory_reset_committed());
        assert(std::string(services.getConfig().reticulumConfig().reticulum_groups[0].name) == "Legacy reset group");
        persistName(services, "ResetOldName Ab!9");
        chat::reticulum::ReticulumNetworkConfig network;
        network.interface_count = 1;
        auto& entry = network.interfaces[0];
        entry.type = chat::reticulum::NetworkInterfaceType::TcpClient;
        std::strcpy(entry.id, "reset-saved-tcp");
        std::strcpy(entry.target_host, "reset-old.example.test");
        assert(services.setReticulumNetworkConfig(network));
        for (const auto* ns : reset::kLegacyNamespaces) assert(storage::put_string(ns, "reset_fixture", "old"));
        assert(storage::put_string(reset_storage::kConfigTransferNamespace, "reset_fixture", "private upload"));
        storage::put_uint("reset_fixture_metadata", "pid", uint32_t(getpid()));
        storage::put_uint("reset_fixture_metadata", "node", services.getSelfNodeId());
        {
            Database db;
            db.sql("CREATE TABLE reset_preserved(id INTEGER PRIMARY KEY,value INTEGER); INSERT INTO reset_preserved VALUES(1,12345);");
            db.sql("CREATE TRIGGER reject_reset BEFORE DELETE ON settings WHEN OLD.namespace='linux_reticulum' BEGIN SELECT RAISE(ABORT,'injected'); END;");
            const auto before = appBlob();
            assert(!reset::begin());
            assert(!reset_storage::factory_reset_committed() && appBlob() == before);
            for (const auto* ns : reset::kLegacyNamespaces)
            {
                std::string text;
                assert(storage::get_string(ns, "reset_fixture", text) && text == "old");
            }
            assert(NetworkStore::load(network) == NetworkStore::Result::Ok && network.interface_count == 1);
            std::string upload;
            assert(storage::get_string(reset_storage::kConfigTransferNamespace, "reset_fixture", upload) && upload == "private upload");
            db.sql("DROP TRIGGER reject_reset;");
        }
        persistName(services, "AfterFailedReset");
        const auto stale = appBlob();
        assert(reset::begin() && reset::begin());
        assert(reset_storage::factory_reset_committed());
        std::string upload;
        assert(!storage::get_string(reset_storage::kConfigTransferNamespace, "reset_fixture", upload));
        assert(!storage::put_string(reset_storage::kConfigTransferNamespace, "reset_fixture", "resurrected"));
        const auto defaults = appBlob();
        assert(!contains(defaults, "AfterFailedReset") && !contains(defaults, "Legacy reset group"));
        assert(storage::get_bool(reset_storage::kAppNamespace, reset_storage::kResetMarker, false));
        for (const auto* ns : reset::kLegacyNamespaces)
        {
            std::string text;
            assert(!storage::get_string(ns, "reset_fixture", text));
            assert(!storage::put_string(ns, "reset_fixture", "resurrected"));
        }
        assert(!storage::put_blob(reset_storage::kAppNamespace, reset_storage::kAppConfigKey, stale.data(), stale.size()));
        assert(!storage::put_blob(reset_storage::kAppNamespace, reset_storage::kAppConfigKey, nullptr, 0));
        const char* keys[]{reset_storage::kAppConfigKey};
        storage::remove_keys(reset_storage::kAppNamespace, keys, 1);
        storage::clear_namespace(reset_storage::kAppNamespace);
        assert(appBlob() == defaults);
        assert(!services.setReticulumNetworkConfig(network));
        assert(!platform::ui::reticulum_network_config::status().supported);
        assert(NetworkStore::load(network) == NetworkStore::Result::Missing);
        reset::finish();
        storage::put_bool("settings", "reset_fixture", true);
        assert(!storage::get_bool("settings", "reset_fixture", false));
        services.saveConfig();
        services.updateCoreServices();
        services.shutdown();
        assert(appBlob() == defaults);
        {
            Database db;
            db.checkPreserved();
        }
        setenv("TRAIL_MATE_RESET_TEST_PHASE", "verify", 1);
    }
    else
    {
        assert(!reset_storage::factory_reset_committed());
        assert(storage::get_uint("reset_fixture_metadata", "pid", 0) == uint32_t(getpid()));
        assert(storage::get_uint("reset_fixture_metadata", "node", 0) == services.getSelfNodeId());
        assert(std::string(services.getConfig().node_name) == "Reset fixture default");
        for (const auto& group : services.getConfig().reticulumConfig().reticulum_groups)
            assert(!chat::hasReticulumDestinationIdentity(group.identity));
        chat::reticulum::ReticulumNetworkConfig network;
        assert(NetworkStore::load(network) == NetworkStore::Result::Missing);
        {
            Database db;
            db.checkPreserved();
        }
        persistName(services, "FreshWritesAllowed");
        services.shutdown();
        if (phase == "verify-missing")
        {
            assert(!contains(appBlob(), "Legacy reset group"));
            std::puts("Factory reset: rollback, quiescent teardown, two real exec restarts, legacy migration suppression and resumed persistence passed");
            return 0;
        }
        assert(phase == "verify");
        // Even a later missing working blob must not reimport a discarded
        // pre-reset SD migration source. The reset marker remains authoritative.
        assert(storage::put_blob(reset_storage::kAppNamespace, reset_storage::kAppConfigKey, nullptr, 0));
        setenv("TRAIL_MATE_RESET_TEST_PHASE", "verify-missing", 1);
    }
    platform::ui::device::restart();
    assert(platform::linux_runtime::application_restart_requested());
    return platform::linux_runtime::finish_application(0, argv);
}
int main(int argc, char* argv[])
{
    if (argc == 3 && !std::strcmp(argv[1], "--child")) return child(argv, argv[2]);
    char pattern[] = "/tmp/trailmate-settings-reset-XXXXXX";
    const auto* directory = mkdtemp(pattern);
    assert(directory);
    const std::filesystem::path root(directory);
    std::filesystem::create_directories(root / "sd/trailmate/reticulum");
    std::ofstream(root / "sd/trailmate/reticulum/groups.tsv") << "version\t1\n1\tLegacy reset group\t0123456789abcdef0123456789abcdef\n";
    const auto pid = fork();
    assert(pid >= 0);
    if (!pid)
    {
        setenv("TRAIL_MATE_RESET_TEST_PHASE", "reset", 1);
        execl("/proc/self/exe", argv[0], "--child", root.c_str(), nullptr);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    std::filesystem::remove_all(root);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
