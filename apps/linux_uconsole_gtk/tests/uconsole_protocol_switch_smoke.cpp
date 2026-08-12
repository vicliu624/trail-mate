#include "app/linux_app_services.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{

void setEnv(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      "trailmate_uconsole_protocol_switch_smoke";
    std::error_code error{};
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "settings", error);
    std::filesystem::create_directories(root / "sd", error);
    std::filesystem::create_directories(root / "cache", error);
    assert(!error);

    setEnv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").string());
    setEnv("TRAIL_MATE_SD_ROOT", (root / "sd").string());
    setEnv("TRAIL_MATE_CACHE_ROOT", (root / "cache").string());
    setEnv("TRAIL_MATE_LORA_DISABLE", "1");
    setEnv("TRAIL_MATE_RUNTIME_MODE", "local");

    {
        trailmate::linux_app::LinuxAppServices services;
        assert(services.initialize());
        assert(services.meshProtocol() == ::chat::MeshProtocol::Reticulum);
        assert(services.switchMeshProtocol(::chat::MeshProtocol::Meshtastic,
                                           true));
        assert(services.meshProtocol() == ::chat::MeshProtocol::Meshtastic);
    }

    {
        trailmate::linux_app::LinuxAppServices restarted;
        assert(restarted.initialize());
        assert(restarted.meshProtocol() == ::chat::MeshProtocol::Meshtastic);
        assert(restarted.switchMeshProtocol(::chat::MeshProtocol::MeshCore,
                                            true));
        assert(restarted.meshProtocol() == ::chat::MeshProtocol::MeshCore);
    }

    {
        trailmate::linux_app::LinuxAppServices restarted;
        assert(restarted.initialize());
        assert(restarted.meshProtocol() == ::chat::MeshProtocol::MeshCore);
    }

    std::filesystem::remove_all(root, error);
    return 0;
}
