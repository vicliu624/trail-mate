#include "app/linux_app_services.h"
#include "app/linux_tms_document.h"
#include "app/linux_tms_store.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/settings_transaction.h"
#include "platform/ui/reticulum_call_runtime.h"
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace call = platform::ui::reticulum_call;
namespace native = platform::linux_runtime;
namespace tms = app::tms;
using Services = trailmate::linux_app::LinuxAppServices;
using Result = Services::SettingsImportResult;
namespace
{
std::mutex mutex;
std::condition_variable changed;
bool hook_entered = false, release_hook = false;
bool exclusive(const uint8_t*)
{
    std::unique_lock<std::mutex> lock(mutex);
    hook_entered = true;
    changed.notify_all();
    changed.wait(lock, []
                 { return release_hook; });
    return true;
}
void test_call_gate()
{
    call::Peer peer;
    peer.link_id[0] = 1;
    peer.destination_hash[0] = 2;
    assert(call::try_begin_configuration_change());
    assert(!call::try_begin_configuration_change());
    assert(!call::begin_incoming_identifying(peer));
    assert(!call::begin_outgoing(peer));
    call::end_configuration_change();
    call::RealtimeHooks hooks;
    hooks.begin_exclusive = exclusive;
    call::set_realtime_hooks(hooks);
    bool admitted = false;
    std::thread dial([&]
                     { admitted = call::begin_outgoing(peer); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, []
                     { return hook_entered; });
    }
    // Call state is still idle while its external admission hook executes.
    assert(call::snapshot().realtime_phase == call::RealtimePhase::Idle);
    assert(!call::try_begin_configuration_change());
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_hook = true;
    }
    changed.notify_all();
    dial.join();
    assert(admitted && !call::try_begin_configuration_change());
    call::hangup();
    assert(!call::try_begin_configuration_change());
    uint8_t closed[call::kHashSize]{};
    assert(call::consume_hangup_request(closed));
    call::notify_link_closed(closed);
    call::service_ui_runtime();
    assert(call::try_begin_configuration_change());
    call::end_configuration_change();
    call::set_realtime_hooks({});
}
} // namespace

int main()
{
    test_call_gate();
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-tms-lifecycle-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    assert(native::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = native::LinuxRuntimeMode::DeviceLocal;
    Services services(options);
    assert(services.initialize());
    // This fixture tests settings/call admission, independently of host audio.
    // Native audio and its focus hooks have their own service fixtures.
    call::set_realtime_hooks({});
    std::strcpy(services.config().node_name, "latest pending edit");
    services.saveConfig();
    std::string snapshot;
    assert(services.exportSettingsDocument(tms::DocumentKind::Working, snapshot));
    auto document = trailmate::linux_app::LinuxTmsDocument::parse(snapshot.data(), snapshot.size(), tms::DocumentKind::Working);
    assert(document && !std::strcmp(document->config.node_name, "latest pending edit"));
    std::strcpy(document->config.node_name, "imported replacement");
    std::string incoming;
    assert(document->write(tms::DocumentKind::Working, incoming));
    assert(services.importSettingsDocument(incoming.data(), incoming.size(), tms::DocumentKind::Working) == Result::Unavailable);
    assert(!native::settings_import_committed());
    {
        native::ApplicationLifecycleOwner owner;
        assert(services.importSettingsDocument("broken", 6, tms::DocumentKind::Working) == Result::InvalidDocument);
        call::Peer peer;
        peer.link_id[0] = 3;
        peer.destination_hash[0] = 4;
        assert(call::begin_incoming_identifying(peer));
        assert(services.importSettingsDocument(incoming.data(), incoming.size(), tms::DocumentKind::Working) == Result::Busy);
        call::notify_link_closed(peer.link_id);
        call::service_ui_runtime();
        sqlite3* db = nullptr;
        assert(sqlite3_open(native::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
        assert(sqlite3_busy_timeout(db, 5000) == SQLITE_OK);
        assert(sqlite3_exec(db, "CREATE TRIGGER reject_import BEFORE INSERT ON settings WHEN NEW.namespace='power' BEGIN SELECT RAISE(ABORT,'injected'); END;", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(services.importSettingsDocument(incoming.data(), incoming.size(), tms::DocumentKind::Working) == Result::StorageError);
        assert(!native::settings_import_committed() && !native::application_restart_requested());
        assert(call::try_begin_configuration_change());
        call::end_configuration_change();
        assert(services.isInitialized());
        assert(!std::strcmp(services.config().node_name, "latest pending edit"));
        assert(sqlite3_exec(db, "DROP TRIGGER reject_import;", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
        std::strcpy(services.config().node_name, "unsaved old edit");
        services.saveConfig();
        assert(services.importSettingsDocument(incoming.data(), incoming.size(), tms::DocumentKind::Working) == Result::RestartPending);
        assert(native::application_restart_requested() && native::settings_import_committed());
        assert(!call::begin_incoming_identifying(peer));
        services.saveConfig();
        services.tick();
        services.updateCoreServices();
        assert(!services.exportSettingsDocument(tms::DocumentKind::Working, snapshot));
        services.shutdown();
        assert(trailmate::linux_app::export_linux_tms_snapshot(tms::DocumentKind::Working, snapshot));
        document = trailmate::linux_app::LinuxTmsDocument::parse(snapshot.data(), snapshot.size(), tms::DocumentKind::Working);
        assert(document && !std::strcmp(document->config.node_name, "imported replacement"));
        assert(native::request_application_exit() && !native::application_restart_requested());
    }
    call::end_configuration_change();
    std::filesystem::remove_all(root);
    std::puts("Native TMS lifecycle: in-flight call admission exclusion, busy/rollback recovery, latest export, restart handoff and stale-save isolation passed");
}
