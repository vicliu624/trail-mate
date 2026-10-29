#include "app/linux_app_services.h"
#include "platform/linux/packet_radio.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-mc-services-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    platform::linux_runtime::PacketRadioHardware hardware;
    hardware.driver = "none";
    assert(platform::linux_runtime::configure_packet_radio(hardware));
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceRealMesh;
    uint32_t meshcore_id = 0, meshtastic_id = 0;
    {
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        meshtastic_id = services.getSelfNodeId();
        assert(meshtastic_id);
        assert(services.switchMeshProtocol(chat::MeshProtocol::MeshCore, false));
        meshcore_id = services.getSelfNodeId();
        assert(meshcore_id && meshcore_id != meshtastic_id);
        // Public service access repeatedly calls ensureServicesReady. It must
        // not overwrite the base ID with the active protocol's derived ID.
        (void)services.getChatService();
        services.applyUserInfo();
        assert(services.getSelfNodeId() == meshcore_id);
        assert(services.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        assert(services.getSelfNodeId() == meshtastic_id);
        (void)services.getChatService();
        assert(services.switchMeshProtocol(chat::MeshProtocol::MeshCore, false));
        assert(services.getSelfNodeId() == meshcore_id);
        services.shutdown();
    }
    {
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        assert(services.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        assert(services.getSelfNodeId() == meshtastic_id);
        assert(services.switchMeshProtocol(chat::MeshProtocol::MeshCore, false));
        assert(services.getSelfNodeId() == meshcore_id);
        services.shutdown();
    }
    std::filesystem::remove_all(root);
}
