#include "app/linux_app_services.h"
#include "platform/ui/settings_store.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <unistd.h>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-config-" + std::to_string(getpid()));
    const auto sd = root / "sd";
    std::filesystem::create_directories(sd / "trailmate/reticulum");
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", sd.c_str(), 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    std::ofstream(sd / "trailmate/reticulum/groups.tsv") << "version\t1\n1\tLegacy group\t0123456789abcdef0123456789abcdef\n";
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
    {
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        assert(std::string(services.getConfig().reticulumConfig().reticulum_groups[0].name) == "Legacy group");
        auto edit = services.beginConfigEdit();
        assert(edit);
        edit.config().mesh_protocol = chat::MeshProtocol::Reticulum;
        auto& group = edit.config().reticulumConfig().reticulum_groups[0];
        std::strcpy(group.name, "CM0 Ab!9");
        edit.commit(app::AppConfigChangeSet::mesh());
        services.applyMeshConfig();
        assert(std::string(services.getConfig().reticulumConfig().reticulum_groups[0].name) == "CM0 Ab!9");
        // Match the explicit update loop used by SDL and Cardputer, without tick().
        bool persisted = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!persisted && std::chrono::steady_clock::now() < deadline)
        {
            services.updateCoreServices();
            std::vector<uint8_t> blob;
            const std::string expected = "CM0 Ab!9";
            persisted = platform::ui::settings_store::get_blob("linux_app_facade", "app_config_v1", blob) &&
                        std::search(blob.begin(), blob.end(), expected.begin(), expected.end()) != blob.end();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        assert(persisted); // Must reach durable storage before shutdown.
        services.shutdown();
    }
    {
        trailmate::linux_app::LinuxAppServices reopened(options);
        assert(reopened.initialize());
        const auto& group = reopened.getConfig().reticulumConfig().reticulum_groups[0];
        assert(group.enabled && std::string(group.name) == "CM0 Ab!9");
        // The unchanged old file must never overwrite a modern saved config.
        reopened.applyMeshConfig();
        assert(std::string(reopened.getConfig().reticulumConfig().reticulum_groups[0].name) == "CM0 Ab!9");
        auto edit = reopened.beginConfigEdit();
        edit.config().reticulumConfig().reticulum_groups[0] = {};
        edit.commit(app::AppConfigChangeSet::mesh());
        reopened.applyMeshConfig();
        reopened.shutdown();
    }
    {
        trailmate::linux_app::LinuxAppServices empty(options);
        assert(empty.initialize());
        assert(!chat::hasReticulumDestinationIdentity(empty.getConfig().reticulumConfig().reticulum_groups[0].identity));
        auto edit = empty.beginConfigEdit();
        edit.config().mesh_protocol = chat::MeshProtocol::Meshtastic;
        edit.commit(app::AppConfigChangeSet::mesh());
        empty.applyMeshConfig();
        empty.shutdown(); // A modern empty list must not resurrect the old file.
    }
    // Recreate a version-2 snapshot with an empty list, as produced by the old
    // Linux owner. Its legacy file must be imported once, even on an inactive protocol.
    std::vector<uint8_t> old_blob;
    assert(platform::ui::settings_store::get_blob("linux_app_facade", "app_config_v1", old_blob));
    assert(old_blob.size() > 8);
    const uint32_t old_version = 2;
    std::memcpy(old_blob.data() + 4, &old_version, sizeof(old_version));
    assert(platform::ui::settings_store::put_blob("linux_app_facade", "app_config_v1", old_blob.data(), old_blob.size()));
    {
        trailmate::linux_app::LinuxAppServices migrated(options);
        assert(migrated.initialize());
        assert(std::string(migrated.getConfig().reticulumConfig().reticulum_groups[0].name) == "Legacy group");
        migrated.shutdown();
    }
    std::vector<uint8_t> upgraded;
    assert(platform::ui::settings_store::get_blob("linux_app_facade", "app_config_v1", upgraded));
    uint32_t version = 0;
    std::memcpy(&version, upgraded.data() + 4, sizeof(version));
    assert(version == 3);
    std::filesystem::remove_all(root);
}
