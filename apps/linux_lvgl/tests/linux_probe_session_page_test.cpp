#include "app/app_facade_access.h"
#include "app/linux_app_facade.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/ui/lora_receive_session.h"
#include "sys/clock.h"
#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace
{
uint32_t clock_ms = 1000;
uint32_t now() { return clock_ms; }
platform::ui::lora::ReceiveSessionSnapshot receiver;
unsigned configurations = 0, transmissions = 0, releases = 0;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void synchronousIo() { throw std::runtime_error("Protocol Probe performed synchronous radio I/O"); }
void pump(uint32_t elapsed)
{
    clock_ms += elapsed;
    lv_tick_inc(elapsed);
    lv_timer_handler();
}
} // namespace
namespace platform::ui::lora
{
bool is_supported() { return true; }
CapabilityStatus capability_status() { return {CapabilityState::Available, "fake receiver"}; }
bool acquire()
{
    synchronousIo();
    return false;
}
bool is_online()
{
    synchronousIo();
    return false;
}
bool configure_receive(float, const ReceiveConfig&)
{
    synchronousIo();
    return false;
}
bool transmit_packet(const uint8_t*, size_t)
{
    synchronousIo();
    return false;
}
bool poll_received_packet(uint8_t*, size_t, ReceivedPacket*)
{
    synchronousIo();
    return false;
}
float read_instant_rssi()
{
    synchronousIo();
    return 0;
}
void release() { synchronousIo(); }
uint32_t request_receive_session(float, const ReceiveConfig&)
{
    ++configurations;
    ++receiver.generation;
    receiver.state = ReceiveSessionState::Configuring;
    receiver.transmit_pending = false;
    return receiver.generation;
}
ReceiveSessionSnapshot receive_session_snapshot() { return receiver; }
bool poll_receive_session(uint32_t generation, uint8_t*, size_t, ReceivedPacket*)
{
    require(generation == receiver.generation && receiver.state == ReceiveSessionState::Ready, "page polled an unready/obsolete receiver");
    return false;
}
bool transmit_receive_session(uint32_t generation, const uint8_t*, size_t size)
{
    require(generation == receiver.generation && receiver.state == ReceiveSessionState::Ready && size, "page transmitted in wrong receiver context");
    ++transmissions;
    receiver.transmit_pending = true;
    return true;
}
void release_receive_session(uint32_t generation)
{
    require(generation == receiver.generation, "page released obsolete receiver");
    ++releases;
    receiver.state = ReceiveSessionState::Idle;
}
} // namespace platform::ui::lora

int main()
{
    namespace runtime = platform::linux_runtime;
    namespace probe = energy_sweep::ui::runtime;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-probe-session-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    for (const char* key : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_CACHE_ROOT"}) setenv(key, root.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    runtime::PacketRadioHardware hardware;
    hardware.driver = "none";
    require(runtime::configure_packet_radio(hardware), "isolated radio setup failed");
    require(runtime::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}), "isolated config setup failed");
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = runtime::LinuxRuntimeMode::DeviceLocal});
        require(facade.initialize(), "facade initialization failed");
        require(app::configFacade().switchMeshProtocol(chat::MeshProtocol::MeshCore, false), "MeshCore selection failed");
        sys::set_millis_provider(now);
        require(probe::text_start(), "probe did not accept asynchronous startup");
        require(configurations == 1, "initial candidate missing");
        pump(60000);
        require(configurations == 1 && !transmissions, "configuration delay consumed dwell/started TX");
        receiver.state = platform::ui::lora::ReceiveSessionState::Ready;
        pump(60);
        pump(1200);
        require(transmissions == 1 && receiver.transmit_pending, "active verification not queued");
        pump(60000);
        require(configurations == 1, "response window expired before TX completed");
        receiver.transmit_pending = false;
        pump(60);
        pump(4500);
        require(configurations == 1, "response window not restarted after TX completion");
        pump(120000);
        require(configurations == 2, "completed candidate did not advance");
        receiver.state = platform::ui::lora::ReceiveSessionState::Failed;
        pump(60);
        probe::TextSnapshot snapshot;
        require(probe::text_snapshot(snapshot) && snapshot.radio_error && !snapshot.scanning, "worker failure did not stop probe");
        require(releases == 1, "failed probe did not release its generation");
        probe::text_stop();
        sys::set_millis_provider(nullptr);
        facade.shutdown();
    }
    lv_deinit();
    std::filesystem::remove_all(root);
}
