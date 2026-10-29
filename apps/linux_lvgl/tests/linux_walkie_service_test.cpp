#include "app/linux_app_facade.h"
#include "platform/audio/audio_focus.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/walkie_service.h"
#include "platform/ui/walkie_runtime.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <thread>

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void settled()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (platform::ui::walkie::is_active())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("walkie worker did not settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
int main()
{
    namespace port = platform::ui::walkie;
    char temporary[] = "/tmp/trailmate-walkie-service-XXXXXX";
    require(mkdtemp(temporary), "temporary directory");
    const std::filesystem::path root(temporary), state = root / "state";
    setenv("TRAIL_MATE_SETTINGS_ROOT", state.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "data").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    unsetenv("TRAIL_MATE_NODE_ID");
    require(port::is_supported() && !port::is_active() && port::get_status().rx_level == 0, "idle state fabricated activity");
    require(!std::filesystem::exists(state), "idle getter performed storage I/O");
    require(!port::start(), "capture started without application identity");
    platform::linux_runtime::PacketRadioHardware hardware;
    hardware.driver = "none";
    require(platform::linux_runtime::configure_packet_radio(hardware), "disabled radio composition");
    {
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal});
        require(facade.initialize(), "application initialization");
        require(facade.getSelfNodeId() != 0, "application has no node identity");
        require(port::start(), "worker request rejected");
        settled();
        const auto status = port::get_status();
        require(!status.tx && status.tx_level == 0 && status.rx_level == 0 && *port::last_error(), "missing radio fabricated audio/ready state");
        require(!platform::audio::music_blocked(), "failed worker leaked audio focus");
        require(port::set_monitor_enabled(true), "monitor request rejected");
        settled();
        require(!port::monitor_enabled(), "failed monitor retained active state");
        port::set_ptt(true);
        require(!port::get_status().tx, "PTT activated a failed session");
        port::stop();
        platform::linux_runtime::shutdown_native_walkie();
        require(!port::start(), "shutdown service accepted new work");
        facade.shutdown();
    }
    std::filesystem::remove_all(root);
}
