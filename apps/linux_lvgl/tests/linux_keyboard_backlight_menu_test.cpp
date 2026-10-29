#include "app/linux_app_facade.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/keyboard_backlight.h"
#include "platform/ui/device_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/startup_ui_shell.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

int main()
{
    namespace fs = std::filesystem;
    namespace runtime = platform::linux_runtime;
    const auto root = fs::temp_directory_path() / ("trailmate-keyboard-menu-" + std::to_string(getpid()));
    fs::create_directories(root / "class/pwmchip74/device");
    fs::create_directories(root / "tree/keyboard");
    fs::create_directory_symlink(root / "tree/keyboard", root / "class/pwmchip74/device/of_node");
    fs::create_directories(root / "class/pwmchip74/pwm0");
    const auto put = [&](const char* file, const char* value)
    { std::ofstream(root / "class/pwmchip74" / file) << value << '\n'; };
    put("npwm", "1");
    put("pwm0/period", "50000");
    put("pwm0/duty_cycle", "5000");
    put("pwm0/enable", "1");
    put("pwm0/polarity", "normal");
    runtime::KeyboardBacklightOptions light;
    light.root = root / "class";
    light.device_tree_root = root / "tree";
    light.of_node = "/keyboard";
    light.initial_period_ns = 50000;
    light.max_duty_permille = 100;
    assert(runtime::configure_keyboard_backlight(light));
    runtime::prepare_keyboard_backlight();
    const auto await = [&](uint8_t level)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!platform::ui::device::supports_keyboard_backlight() || platform::ui::device::keyboard_backlight() != level)
        {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    await(255);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    runtime::PacketRadioHardware radio;
    radio.driver = "none";
    assert(runtime::configure_packet_radio(radio));
    assert(runtime::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = runtime::LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        ui::startup_ui_shell::Hooks hooks;
        hooks.ux_pack_id = "t_display_p4_touch";
        ui::startup_ui_shell::prepareBootResources();
        assert(ui::startup_ui_shell::initializeMenuSkeleton(hooks));
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
        assert(!ui::menu_runtime::handleShortcutKey('k', 0));
        for (uint8_t level : {uint8_t(0), uint8_t(127), uint8_t(255)})
        {
            assert(ui::menu_runtime::handleShortcutKey('k', 1));
            await(level);
        }
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::App);
        assert(!ui::menu_runtime::handleShortcutKey('k', 1));
        assert(platform::ui::device::keyboard_backlight() == 255);
        facade.shutdown();
    }
    runtime::shutdown_keyboard_backlight();
    lv_deinit();
    fs::remove_all(root);
}
