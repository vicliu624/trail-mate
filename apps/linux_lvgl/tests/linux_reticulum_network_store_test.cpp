#include "app/linux_app_services.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/linux/runtime_paths.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sqlite3.h>
#include <unistd.h>

namespace r = chat::reticulum;
using Services = trailmate::linux_app::LinuxAppServices;
using Store = platform::linux_runtime::LinuxReticulumNetworkStore;

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-network-store-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
    r::ReticulumNetworkConfig network, output;
    network.interface_count = 5;
    for (size_t i = 0; i < network.interface_count; ++i)
    {
        auto& entry = network.interfaces[i];
        std::snprintf(entry.id, sizeof(entry.id), "saved-%zu", i);
        entry.type = i == 0 ? r::NetworkInterfaceType::IntegratedLoRa : i == 1 ? r::NetworkInterfaceType::Auto
                                                                               : r::NetworkInterfaceType::TcpClient;
        std::snprintf(entry.target_host, sizeof(entry.target_host), "gateway-%zu.example.test", i);
        entry.target_port = uint16_t(6000 + i);
    }
    std::strcpy(network.interfaces[2].access.network_name, "private mesh");
    std::strcpy(network.interfaces[2].access.passphrase, "secret %:=\n");
    network.interfaces[2].access.ifac_size_bits = 128;
    network.propagation.sync_interval_s = 3600;
    // Codec use must not require a database or mutate output on rejection.
    std::string projection;
    assert(Store::encode(network, app::AppConfig{}, projection));
    assert(Store::decode(projection.data(), projection.size(), output));
    assert(output.interfaces[2].access == network.interfaces[2].access);
    assert(!Store::decode(nullptr, projection.size(), output));
    assert(!Store::decode("broken", 6, output));
    assert(output.interface_count == 5 && output.interfaces[2].access == network.interfaces[2].access);
    auto nul_projection = projection;
    nul_projection[nul_projection.size() / 2] = 0;
    assert(!Store::decode(nul_projection.data(), nul_projection.size(), output));
    auto invalid_projection = network;
    std::strcpy(invalid_projection.interfaces[3].id, invalid_projection.interfaces[2].id);
    const auto preserved_projection = projection;
    assert(!Store::encode(invalid_projection, app::AppConfig{}, projection));
    assert(projection == preserved_projection);
    {
        Services services(options);
        assert(services.initialize());
        assert(!services.getReticulumNetworkConfig(&output));
        assert(services.setReticulumNetworkConfig(network));
        assert(services.getReticulumNetworkConfig(&output) && output.interface_count == 5);
        assert(Store::load(output) == Store::Result::Ok && output.interfaces[2].access == network.interfaces[2].access);
        auto invalid = network;
        std::strcpy(invalid.interfaces[3].id, invalid.interfaces[2].id);
        assert(!services.setReticulumNetworkConfig(invalid));
        assert(Store::load(output) == Store::Result::Ok && !std::strcmp(output.interfaces[3].id, "saved-3"));

        // An actual SQLite write rejection must preserve both durable and live
        // snapshots; successful validation alone must never activate an edit.
        sqlite3* db = nullptr;
        assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
        assert(sqlite3_busy_timeout(db, 5000) == SQLITE_OK);
        assert(sqlite3_exec(db, "CREATE TRIGGER reject_network BEFORE INSERT ON settings WHEN NEW.namespace='linux_reticulum' BEGIN SELECT RAISE(ABORT,'injected'); END;", nullptr, nullptr, nullptr) == SQLITE_OK);
        auto changed = network;
        changed.interfaces[2].target_port = 9999;
        assert(!services.setReticulumNetworkConfig(changed));
        assert(services.getReticulumNetworkConfig(&output) && output.interfaces[2].target_port == 6002);
        assert(Store::load(output) == Store::Result::Ok && output.interfaces[2].target_port == 6002);
        assert(sqlite3_exec(db, "DROP TRIGGER reject_network;", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
        services.shutdown();
    }
    {
        Services reopened(options);
        assert(reopened.initialize());
        assert(reopened.getReticulumNetworkConfig(&output) && output.interface_count == 5);
        assert(output.interfaces[2].access == network.interfaces[2].access && output.propagation.sync_interval_s == 3600);
        reopened.applyMeshConfig();
        assert(reopened.setReticulumNetworkConfig(r::ReticulumNetworkConfig{}));
        reopened.shutdown();
    }
    {
        Services reopened(options);
        assert(reopened.initialize());
        assert(reopened.getReticulumNetworkConfig(&output) && output.interface_count == 0);
        reopened.shutdown();
    }
    assert(platform::ui::settings_store::put_blob("linux_reticulum", "network_tms_v1", "broken", 6));
    output = network;
    assert(Store::load(output) == Store::Result::Invalid && output.interface_count == 5);
    {
        Services reopened(options);
        assert(reopened.initialize());
        assert(reopened.getReticulumNetworkConfig(&output) && output.interface_count == 0);
        reopened.shutdown();
    }
    std::filesystem::remove_all(root);
    std::puts("Linux network config: full durable reload, empty list, invalid input, SQLite failure and corrupt-document isolation passed");
}
