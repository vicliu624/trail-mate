#include "app/linux_app_facade.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "ui/presentation_sources/runtime_settings_source.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <unistd.h>

namespace network = platform::ui::reticulum_network_config;
namespace sources = ui::presentation_sources;
using Facade = trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade;
using Store = platform::linux_runtime::LinuxReticulumNetworkStore;

bool edit(const char* key, const char* value)
{
    ui::settings::SettingsPatchView patch;
    ui::copyText(patch.key, key);
    ui::copyText(patch.value, value);
    return sources::runtime_settings_action_sink().applySetting(patch).ok;
}
std::string value(const char* key)
{
    ui::settings::SettingsSnapshot snapshot;
    assert(sources::runtime_settings_source().buildSettingsSnapshot(snapshot));
    for (size_t i = 0; i < snapshot.section_count; ++i)
        for (size_t j = 0; j < snapshot.sections[i].option_count; ++j)
        {
            const auto& option = snapshot.sections[i].options[j];
            if (!std::strcmp(option.key.c_str(), key)) return option.value_label.c_str();
        }
    assert(false && "Required settings option was omitted");
    return {};
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-network-settings-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
    {
        Facade facade(options);
        assert(facade.initialize());
        assert(network::status().supported && network::status().source == network::Source::Defaults);
        assert(network::active().interface_count == 5);
        auto config = facade.beginConfigEdit();
        config.config().mesh_protocol = chat::MeshProtocol::Reticulum;
        config.config().reticulumConfig().reticulum_wifi_gateway_enabled = false;
        config.config().reticulumConfig().reticulum_lora_enabled = false;
        config.commit(app::AppConfigChangeSet::mesh());
        facade.applyMeshConfig();
        // Editing the saved candidates is independent of the bearer policy.
        assert(network::restoreDefaultTcpEndpoints());
        assert(value("rt_tcp_slot") == "1");
        assert(value("rt_wifi_host") == "sydney.reticulum.au");
        assert(edit("rt_tcp_2_host", "MixedCase.example.test"));
        assert(edit("rt_tcp_2_port", "65535"));
        assert(edit("rt_tcp_slot", "next"));
        assert(value("rt_tcp_slot") == "2");
        assert(value("rt_wifi_host") == "MixedCase.example.test");
        assert(value("rt_wifi_port") == "65535");
        const auto generation = network::status().generation;
        assert(!edit("rt_wifi_host", "bad host"));
        assert(!edit("rt_wifi_port", "65536"));
        assert(!edit("rt_wifi_port", "0"));
        assert(network::status().generation == generation);
        assert(edit("rt_wifi_host", ""));
        assert(value("rt_wifi_host") == "rmap.world");
        assert(edit("rt_tcp_1_host", ""));
        assert(edit("rt_tcp_1_host", ""));
        chat::reticulum::ReticulumNetworkConfig saved;
        assert(Store::load(saved) == Store::Result::Ok);
        for (size_t i = 0; i < saved.interface_count; ++i)
            assert(saved.interfaces[i].type != chat::reticulum::NetworkInterfaceType::TcpClient);
        assert(!edit("rt_tcp_3_host", "skip.example.test"));
        facade.shutdown();
        assert(!network::status().supported);
        assert(!network::updateTcpEndpoint(0, "after-shutdown.example.test", 4242));
    }
    {
        Facade reopened(options);
        assert(reopened.initialize());
        assert(network::status().source == network::Source::Tms);
        for (size_t i = 0; i < network::active().interface_count; ++i)
            assert(network::active().interfaces[i].type != chat::reticulum::NetworkInterfaceType::TcpClient);
        assert(edit("rt_tcp_1_host", "restart.example.test"));
        assert(edit("rt_tcp_1_port", "5252"));
        reopened.shutdown();
    }
    {
        Facade reopened(options);
        assert(reopened.initialize());
        assert(edit("rt_tcp_slot", "previous"));
        assert(value("rt_wifi_host") == "restart.example.test" && value("rt_wifi_port") == "5252");
        reopened.shutdown();
    }
    std::filesystem::remove_all(root);
}
