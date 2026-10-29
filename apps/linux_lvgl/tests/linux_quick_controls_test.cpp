#include "lvgl.h"
#include "music/service.h"
#include "platform/linux/music_service.h"
#include "platform/linux/packet_radio.h"
#include "platform/ui/quick_controls_runtime.h"
#include "ui_presentation/controls/quick_controls.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unistd.h>

int main()
{
    char name[] = "/tmp/trailmate-controls-test-XXXXXX";
    if (!mkdtemp(name)) return 1;
    const std::filesystem::path root(name);
    setenv("TRAIL_MATE_SD_ROOT", (root / "media").c_str(), 1);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "state").c_str(), 1);
    platform::linux_runtime::PacketRadioHardware hardware;
    hardware.driver = "none";
    platform::linux_runtime::configure_packet_radio(hardware);
    lv_init();
    auto* source = platform::ui::quick_controls_source();
    ::ui::controls::Snapshot state;
    if (!source || !source->snapshot(state) || std::filesystem::exists(root / "media")) return 2;
    using ::ui::controls::Control;
    if (source->set(static_cast<Control>(200), 1) || source->set(Control::Wifi, 2) || source->set(Control::Brightness, 9)) return 3;
    source->set(Control::Volume, 30);
    source->set(Control::Volume, 73); // Latest pending intent must win.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool applied = false;
    while (std::chrono::steady_clock::now() < until)
    {
        platform::ui::service_quick_controls();
        source->snapshot(state);
        if (state.volume_supported && state.volume == 73)
        {
            applied = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    platform::linux_runtime::shutdown_linux_music();
    lv_deinit();
    std::filesystem::remove_all(root);
    return applied ? 0 : 4;
}
