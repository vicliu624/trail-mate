#include "app/linux_app_facade.h"
#include "chat/linux_reticulum_network_store.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/ui/device_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/startup_ui_shell.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace
{
uint32_t clock_ms = 0;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
lv_obj_t* pending_label(lv_obj_t* object)
{
    if (lv_obj_check_type(object, &lv_label_class) && std::strstr(lv_label_get_text(object), "pending-memory")) return object;
    for (unsigned index = 0; index < lv_obj_get_child_count(object); ++index)
        if (auto* label = pending_label(lv_obj_get_child(object, index))) return label;
    return nullptr;
}
} // namespace
int main(int argc, char** argv)
{
    require(argc == 2, "refresh mode required");
    char pattern[] = "/tmp/trailmate-menu-refresh-XXXXXX";
    const auto* root = mkdtemp(pattern);
    require(root, "temporary root failed");
    setenv("TRAIL_MATE_SD_ROOT", root, 1);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root, 1);
    setenv("TRAIL_MATE_BATTERY_LEVEL", "-1", 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    platform::linux_runtime::PacketRadioHardware radio;
    radio.driver = "none";
    require(platform::linux_runtime::configure_packet_radio(radio), "radio setup failed");
    require(platform::linux_runtime::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}),
            "isolated network setup failed");
    lv_init();
    lv_tick_set_cb([]
                   { return clock_ms; });
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal});
        require(facade.initialize(), "facade initialization failed");
        ui::startup_ui_shell::Hooks hooks;
        hooks.ux_pack_id = "t_display_p4_touch";
        uint32_t period = 60000;
        if (!std::strcmp(argv[1], "cached")) hooks.status_refresh_interval_ms = period = 1000;
        else if (!std::strcmp(argv[1], "zero"))
        {
            hooks.status_refresh_interval_ms = 0;
            period = 1000;
        }
        else if (!std::strcmp(argv[1], "large")) hooks.status_refresh_interval_ms = std::numeric_limits<uint32_t>::max();
        else require(!std::strcmp(argv[1], "default"), "unknown refresh mode");
        ui::startup_ui_shell::prepareBootResources();
        require(ui::startup_ui_shell::initializeMenuSkeleton(hooks), "menu initialization failed");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!platform::ui::device::memory_stats().ram_total_bytes)
        {
            require(std::chrono::steady_clock::now() < deadline, "memory sample not published");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Model a first paint made before asynchronous memory discovery. No
        // battery is present, so memory refresh must work independently of it.
        ui::menu_layout::set_bottom_bar_ram_text("pending-memory");
        auto* ram = pending_label(lv_screen_active());
        require(ram, "RAM label not found");
        lv_timer_handler();
        clock_ms = period - 1;
        lv_timer_handler();
        require(std::strstr(lv_label_get_text(ram), "pending-memory"), "status refreshed before configured deadline");
        clock_ms = period;
        lv_timer_handler();
        const auto* value = lv_label_get_text(ram);
        require(!std::strstr(value, "pending-memory") && std::strchr(value, '/') && !std::strstr(value, "/0K"),
                "cached memory was not rendered at status deadline without a battery");
        facade.shutdown();
    }
    lv_deinit();
    std::filesystem::remove_all(root);
}
