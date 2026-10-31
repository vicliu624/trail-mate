#include "app/linux_app_facade.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/packet_radio.h"
#include "platform/ui/fm_runtime.h"
#include "ui/app_catalog_builder.h"
#include "ui/app_runtime.h"
#include "ui/input/pager_navigation.h"
#include "ui/screens/fm/fm_page.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace runtime = platform::ui::fm;
namespace fixture
{
runtime::Snapshot state;
bool supported = true;
unsigned starts = 0, stops = 0;
} // namespace fixture
namespace platform::ui::fm
{
bool is_supported() { return fixture::supported; }
bool start(uint32_t hz, bool exception)
{
    assert(!fixture::state.active && !fixture::state.cleanup_failed);
    ++fixture::starts;
    fixture::state.active = true;
    fixture::state.phase = Phase::Starting;
    fixture::state.error = Error::None;
    fixture::state.frequency_hz = hz;
    fixture::state.calibration_exception_requested = exception;
    ++fixture::state.session;
    return true;
}
void request_stop()
{
    ++fixture::stops;
    if (fixture::state.active || fixture::state.cleanup_failed)
    {
        fixture::state.active = true;
        fixture::state.phase = Phase::Stopping;
    }
}
void set_volume(unsigned percent) { fixture::state.volume = percent; }
Snapshot snapshot() { return fixture::state; }
void wait_until_finished() { assert(false); } // A UI callback must never join.
} // namespace platform::ui::fm
static lv_obj_t* find(lv_obj_t* root, const char* text, bool partial = false)
{
    if (lv_obj_check_type(root, &lv_label_class) &&
        (partial ? std::strstr(lv_label_get_text(root), text) != nullptr : !std::strcmp(lv_label_get_text(root), text))) return root;
    for (uint32_t n = 0; n < lv_obj_get_child_count(root); ++n)
        if (auto* result = find(lv_obj_get_child(root, n), text, partial)) return result;
    return nullptr;
}
static void click(const char* caption)
{
    auto* label = find(lv_screen_active(), caption);
    assert(label);
    auto* target = lv_obj_has_flag(label, LV_OBJ_FLAG_CLICKABLE) ? label : lv_obj_get_parent(label);
    assert(!lv_obj_has_state(target, LV_STATE_DISABLED));
    const auto before = std::chrono::steady_clock::now();
    lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100));
}
static void refresh()
{
    lv_tick_inc(120);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}
static void bounds(lv_obj_t* root, int width, int height)
{
    if (lv_obj_check_type(root, &lv_button_class))
    {
        lv_area_t box;
        lv_obj_get_coords(root, &box);
        assert(box.x1 >= 0 && box.y1 >= 0 && box.x2 < width && box.y2 < height);
    }
    for (uint32_t n = 0; n < lv_obj_get_child_count(root); ++n) bounds(lv_obj_get_child(root, n), width, height);
}
int main(int argc, char** argv)
{
    assert(argc == 4);
    const int width = std::stoi(argv[1]), height = std::stoi(argv[2]);
    const auto output = std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(output);
    for (auto* name : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_CACHE_ROOT"}) setenv(name, output.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    platform::linux_runtime::PacketRadioHardware isolated;
    isolated.driver = "none";
    assert(platform::linux_runtime::configure_packet_radio(isolated));
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.ux_pack = "t_display_p4_touch";
        options.width = width;
        options.height = height;
        options.fullscreen = false;
        platform::desktop::SdlLvglHost host(options);
        trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade facade({.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal});
        assert(facade.initialize());
        if (!app_g) app_g = lv_group_create();
        fixture::supported = false;
        assert(!::ui::fm::application());
        fixture::supported = true;
        auto* app = ::ui::fm::application();
        assert(app && !std::strcmp(app->stable_id(), "fm_radio"));
        const auto catalog = ::ui::app_catalog_builder::build({});
        unsigned entries = 0;
        for (size_t n = 0; n < ::ui::catalogCount(catalog); ++n)
            if (::ui::catalogAt(catalog, n) == app) ++entries;
        assert(entries == 1);
        ui_switch_to_app(app, lv_screen_active());
        refresh();
        assert(find(lv_screen_active(), "Selected: 100.8 MHz"));
        auto* watermark = lv_label_create(lv_layer_top());
        lv_label_set_text(watermark, "UI CONTRACT FIXTURE - NO RF / NO AUDIO");
        lv_obj_align(watermark, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((output / "fm-ready.bmp").string());
        click("Vol +");
        assert(fixture::state.volume == 55);
        click("Calibration: strict");
        click("Start");
        assert(fixture::starts == 1 && fixture::state.calibration_exception_requested);
        fixture::state.phase = runtime::Phase::PcmSubmitted;
        fixture::state.pcm_samples = 1234;
        refresh();
        assert(find(lv_screen_active(), "PCM submitted (not station lock)", true));
        click("] +0.1 MHz");
        click("] +0.1 MHz");
        assert(fixture::starts == 1 && fixture::stops == 2 && fixture::state.active);
        fixture::state.active = false;
        fixture::state.phase = runtime::Phase::Stopped;
        refresh();
        assert(fixture::starts == 2 && fixture::state.frequency_hz == 101000000);
        click("] +0.1 MHz");
        fixture::state.active = false;
        fixture::state.phase = runtime::Phase::Error;
        fixture::state.error = runtime::Error::Restore;
        refresh();
        assert(fixture::starts == 2);
        assert(find(lv_screen_active(), "Start/retune rejected", true));
        fixture::state.error = runtime::Error::Cleanup;
        fixture::state.cleanup_failed = true;
        refresh();
        click("Retry cleanup");
        assert(fixture::state.active && fixture::state.phase == runtime::Phase::Stopping);
        fixture::state.active = false;
        refresh();
        auto* retry = find(lv_screen_active(), "Retry cleanup");
        assert(retry);
        uint32_t space = ' ';
        const auto retry_count = fixture::stops;
        lv_obj_send_event(lv_obj_get_parent(retry), LV_EVENT_KEY, &space);
        assert(fixture::stops == retry_count + 1 && fixture::state.active);
        fixture::state.active = false;
        fixture::state.phase = runtime::Phase::Error;
        refresh();
        assert(find(lv_screen_active(), "codec still reserved", true));
        assert(find(lv_screen_active(), "Final chip flags: not read / pending", true));
        bounds(lv_screen_active(), width, height);
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((output / "fm-page.bmp").string());
        click("H Help");
        assert(lv_group_get_default() != app_g);
        assert(find(lv_layer_top(), "Start / stop independent FM receiver") || find(lv_screen_active(), "Start / stop independent FM receiver"));
        lv_refr_now(nullptr);
        host.present();
        host.saveScreenshot((output / "fm-help.bmp").string());
        uint32_t help_key = 'h';
        auto* modal_focus = lv_group_get_focused(lv_group_get_default());
        assert(modal_focus);
        lv_obj_send_event(modal_focus, LV_EVENT_KEY, &help_key);
        assert(lv_group_get_default() == app_g);
        auto* help_button = find(lv_screen_active(), "H Help");
        lv_obj_send_event(lv_obj_get_parent(help_button), LV_EVENT_KEY, &help_key);
        assert(lv_group_get_default() != app_g);
        uint32_t escape = LV_KEY_ESC;
        lv_obj_send_event(lv_group_get_focused(lv_group_get_default()), LV_EVENT_KEY, &escape);
        assert(lv_group_get_default() == app_g && ui_get_active_app() == app);
        click("H Help");
        click(LV_SYMBOL_CLOSE);
        assert(lv_group_get_default() == app_g && ui_get_active_app() == app);
        click("H Help"); // Exit must also close a still-open modal.
        const auto before = std::chrono::steady_clock::now();
        const auto stopped = fixture::stops;
        app->exit(lv_screen_active());
        assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100) && fixture::stops == stopped + 1);
        refresh();
        assert(fixture::starts == 2);
        ui_detach_app(app);
        lv_obj_delete(watermark);
        facade.shutdown();
    }
    lv_deinit();
}
