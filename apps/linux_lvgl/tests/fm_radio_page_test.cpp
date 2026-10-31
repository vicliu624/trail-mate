#include "app/linux_app_facade.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/packet_radio.h"
#include "ui/app_runtime.h"
#include "ui/fm_radio_app.h"
#include "ui/input/pager_navigation.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

using namespace platform::linux_runtime;
static lv_obj_t* find(lv_obj_t* root, const char* text, bool partial = false)
{
    if (lv_obj_check_type(root, &lv_label_class) &&
        (partial ? std::strstr(lv_label_get_text(root), text) != nullptr : !std::strcmp(lv_label_get_text(root), text))) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* result = find(lv_obj_get_child(root, i), text, partial)) return result;
    return nullptr;
}
static void click(const char* text)
{
    auto* label = find(lv_screen_active(), text);
    assert(label);
    const auto before = std::chrono::steady_clock::now();
    // TopBar Help is itself clickable; button captions delegate to the button.
    lv_obj_send_event(lv_obj_has_flag(label, LV_OBJ_FLAG_CLICKABLE) ? label : lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100));
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto output = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(output);
    for (auto* name : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_CACHE_ROOT"}) setenv(name, output.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    PacketRadioHardware isolated;
    isolated.driver = "none";
    assert(configure_packet_radio(isolated));
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = 1232;
        options.height = 568;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        if (!app_g) app_g = lv_group_create();
        auto radio = make_unavailable_packet_radio("NO HARDWARE - UI FAILURE PATH TEST");
        auto session = std::make_unique<FmSession>(*radio, []
                                                   { return true; });
        auto* state = session.get();
        FmRadioApp page(std::move(session));
        ui_switch_to_app(&page, lv_screen_active());
        assert(ui_get_active_app() == &page);
        click("] +0.1 MHz");
        assert(find(lv_screen_active(), "100.9 MHz"));
        click("[ -0.1 MHz");
        click("Vol +");
        assert(find(lv_screen_active(), "Volume 55%"));
        click("IMG_CAL exception: OFF");
        assert(find(lv_screen_active(), "IMG_CAL exception: ON"));
        click("Start");
        state->waitUntilFinished();
        assert(state->snapshot().error == FmSession::Error::RadioAcquire);
        assert(state->snapshot().frequency_hz == 100800000);
        for (unsigned n = 0; n < 25; ++n)
        {
            lv_tick_inc(5);
            lv_timer_handler();
        }
        assert(find(lv_screen_active(), "Radio unavailable or busy", true));
        assert(find(lv_screen_active(), "Final chip flags: not read / pending", true));
        auto* watermark = lv_label_create(lv_layer_top());
        lv_label_set_text(watermark, "NO HARDWARE - ERROR PATH REVIEW");
        lv_obj_align(watermark, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((output / "fm-unavailable.bmp").string());
        click("H Help");
        assert(::ui::input::pager::help_open());
        assert(find(lv_layer_top(), "Start / stop independent FM receiver") ||
               find(lv_screen_active(), "Start / stop independent FM receiver"));
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((output / "fm-help.bmp").string());
        const auto before = std::chrono::steady_clock::now();
        page.exit(lv_screen_active());
        assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100));
        ui_detach_app(&page);
        lv_obj_delete(watermark);
        facade.shutdown();
    }
    lv_deinit();
}
