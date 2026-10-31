#include "app/linux_app_facade.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/packet_radio.h"
#include "ui/app_runtime.h"
#include "ui/assets/radio_icons.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include <array>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <filesystem>
#include <thread>
#include <unistd.h>

struct MenuFixtureApp final : AppScreen
{
    const char* stable_id() const override { return "menu_scroll_fixture"; }
    const char* name() const override { return "TEST ICON"; }
    const lv_image_dsc_t* icon() const override { return &fm_radio_icon; }
    void enter(lv_obj_t*) override { assert(false); }
    void exit(lv_obj_t*) override {}
};
struct Pointer
{
    lv_point_t point{};
    lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};
static void tick(unsigned ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do
    {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < until);
}
static void swipe(lv_indev_t* input, Pointer& pointer, int x, int y, int dx, int dy)
{
    pointer.point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    pointer.state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(input);
    tick(40);
    if (auto* state = ui::menu::dashboard::dashboard_state())
        std::fprintf(stderr, "Pointer start: x=%d y=%d tracked=%u inside=%u active=%u scene=%u overlay=%u group=%u\n", x, y,
                     unsigned(state->pressed), unsigned(state->inside_dashboard), unsigned(state->active),
                     unsigned(ui::menu_runtime::currentScene()), unsigned(ui_is_overlay_active()), unsigned(lv_group_get_default() == menu_g));
    for (int step = 1; step <= 8; ++step)
    {
        pointer.point = {static_cast<lv_coord_t>(x + dx * step / 8), static_cast<lv_coord_t>(y + dy * step / 8)};
        lv_indev_read(input);
        tick(20);
        if (step == 8)
            if (auto* state = ui::menu::dashboard::dashboard_state())
                std::fprintf(stderr, "Pointer drag: slide=%u offset=%d tracked=%u page=%u\n", unsigned(state->slide), state->slide_offset,
                             unsigned(state->pressed), unsigned(state->page));
    }
    pointer.state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(input);
    tick(400);
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto output = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(output);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    for (const auto* key : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_CACHE_ROOT"}) setenv(key, output.c_str(), 1);
    platform::linux_runtime::PacketRadioHardware radio;
    radio.driver = "none";
    assert(platform::linux_runtime::configure_packet_radio(radio));
    lv_init();
    lv_tick_set_cb([]() -> uint32_t
                   { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now().time_since_epoch())
                                                      .count()); });
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        std::fprintf(stderr, "Menu scroll fixture: services ready\n");
        Pointer pointer;
        auto* input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(input, &pointer);
        lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data)
                             {
            const auto& value = *static_cast<Pointer*>(lv_indev_get_user_data(device));
            data->point = value.point;
            data->state = value.state; });
        std::array<MenuFixtureApp, 24> apps;
        AppScreen* entries[24];
        for (size_t index = 0; index < apps.size(); ++index) entries[index] = &apps[index];
        auto state = ui::makeStaticAppCatalogState(entries);
        ui::menu_layout::InitOptions init;
        init.apps = ui::makeStaticAppCatalog(&state);
        std::fprintf(stderr, "Menu scroll fixture: creating menu\n");
        ui::menu_layout::init(init);
        std::fprintf(stderr, "Menu scroll fixture: menu created\n");
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
        tick(200);
        std::fprintf(stderr, "Menu scroll fixture: first frame ready\n");
        std::signal(SIGALRM, [](int)
                    { void* frames[48];
                      const int count = backtrace(frames, 48);
                      backtrace_symbols_fd(frames, count, STDERR_FILENO);
                      std::_Exit(124); });
        alarm(10);
        auto* dashboard = ui::menu::dashboard::dashboard_state();
        assert(dashboard && dashboard->dock_right);
        auto* grid = dashboard->menu_grid;
        assert(lv_obj_get_child_count(grid) == apps.size());
        assert(lv_obj_get_scroll_dir(grid) == LV_DIR_VER);
        assert(lv_obj_get_scroll_bottom(grid) > 0);
        const auto initial_page = dashboard->page;
        lv_area_t area;
        lv_obj_get_coords(grid, &area);
        swipe(input, pointer, area.x1 + 60, area.y2 - 35, 0, -220);
        std::fprintf(stderr, "Menu scroll fixture: vertical swipe complete\n");
        assert(lv_obj_get_scroll_y(grid) > 0 && lv_obj_get_scroll_x(grid) == 0);
        assert(dashboard->page == initial_page);
        lv_obj_scroll_to_y(grid, lv_obj_get_scroll_y(grid) + lv_obj_get_scroll_bottom(grid), LV_ANIM_OFF);
        tick(40);
        std::fprintf(stderr, "Menu scroll fixture: grid bottom reached\n");
        lv_area_t last;
        lv_obj_get_coords(lv_obj_get_child(grid, 23), &last);
        assert(last.y2 <= area.y2 && last.y1 >= area.y1);
        const auto scroll = lv_obj_get_scroll_y(grid);
        lv_obj_get_coords(dashboard->panel, &area);
        std::fprintf(stderr, "Menu scroll fixture: starting widget swipe\n");
        swipe(input, pointer, area.x2 - 40, (area.y1 + area.y2) / 2, -180, 0);
        std::fprintf(stderr, "Menu scroll fixture: widget swipe complete\n");
        assert(dashboard->page == initial_page + 1);
        assert(lv_obj_get_scroll_y(grid) == scroll && lv_obj_get_scroll_x(grid) == 0);
        swipe(input, pointer, area.x1 + 40, (area.y1 + area.y2) / 2, 180, 0);
        assert(dashboard->page == initial_page);
        assert(fm_radio_icon.header.w == 64 && fm_radio_icon.header.h == 64);
        assert(oneseg_tv_icon.header.w == 64 && oneseg_tv_icon.header.h == 64);
        alarm(0);
        ui::menu::dashboard::setActive(false);
        lv_indev_delete(input);
        lv_obj_clean(lv_screen_active());
    }
    lv_deinit();
}
