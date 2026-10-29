#include "lvgl.h"
#include "platform/desktop/screen_input_router.h"
#include "platform/ui/settings_store.h"
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace
{
namespace screen = platform::ui::screen;
using namespace platform::linux_runtime;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template <class Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        require(std::chrono::steady_clock::now() < deadline, "screen runtime condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
std::thread::id ui_thread;
unsigned shown = 0, hidden = 0, presented = 0, menus = 0, keys = 0, commits = 0;
void show()
{
    require(std::this_thread::get_id() == ui_thread && screen::is_saver_active(), "saver callback thread/state invalid");
    ++shown;
}
void hide()
{
    require(std::this_thread::get_id() == ui_thread, "hide off UI thread");
    ++hidden;
}
void present() { ++presented; }
void menu() { ++menus; }
void key(platform::desktop::SdlLvglHost& host, SDL_Keycode code, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = code;
    require(SDL_PushEvent(&event) == 1 && host.pump(), "key injection failed");
}
void pointer(platform::desktop::SdlLvglHost& host, int y, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 100;
    event.button.y = y;
    require(SDL_PushEvent(&event) == 1 && host.pump(), "pointer injection failed");
}
struct Compositor
{
    std::mutex mutex;
    std::condition_variable changed;
    bool on = true, available = true, ignore_off = false, block_off = false, entered = false;
    unsigned wake_commands = 0;
    ProcessResult run(const std::vector<std::string>& args, const std::atomic_bool*)
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(std::this_thread::get_id() != ui_thread, "power I/O on UI thread");
        if (args.size() == 1) return {0, false, available ? (on ? "test-panel on\n" : "test-panel off\n") : ""};
        require(args.size() == 3 && args[2] == "test-panel", "unexpected power command");
        if (args[1] == "--off" && block_off)
        {
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&]
                         { return !block_off; });
        }
        if (args[1] == "--on")
        {
            on = true;
            ++wake_commands;
        }
        else if (!ignore_off) on = false;
        return {0, false, {}};
    }
    bool powered()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return on;
    }
    unsigned wakes()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return wake_commands;
    }
};
} // namespace

int main()
{
    ui_thread = std::this_thread::get_id();
    char pattern[] = "/tmp/trailmate-screen-runtime-XXXXXX";
    require(mkdtemp(pattern), "temporary root failed");
    const std::filesystem::path root(pattern);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    require(!screen::supports_app_timeout_setting(), "unconfigured port advertised sleep");
    std::atomic<uint32_t> clock{0};
    Compositor compositor;
    auto power = std::make_shared<OutputPowerRuntime>(OutputPowerOptions{}, [&](const auto& args, const auto* cancel)
                                                      { return compositor.run(args, cancel); });
    await([&]
          { return power->snapshot().available; });
    require(configure_screen_runtime({power, screen::ResumeMethod::SwipeUp, [&]
                                      { return clock.load(); }}),
            "screen configuration failed");
    require(!configure_screen_runtime({}), "duplicate configuration accepted");
    screen::Hooks hooks;
    hooks.show_screen_saver = show;
    hooks.hide_screen_saver = hide;
    hooks.present_screen_saver = present;
    hooks.show_main_menu = menu;
    screen::init(hooks);
    screen::set_timeout_ms(10000);
    require(screen::supports_app_timeout_setting() && screen::resume_method() == screen::ResumeMethod::SwipeUp,
            "capability/resume policy unavailable");
    require(platform::ui::settings_store::get_uint("settings", "screen_timeout", 0) == 10000, "timeout not persisted");
    lv_init();
    {
        platform::desktop::DeviceOptions options;
        options.width = 320;
        options.height = 240;
        options.hardware_keyboard = true;
        options.fullscreen = false;
        options.ux_pack = "t_display_p4_touch";
        platform::desktop::ScreenInputRouter router(240);
        platform::desktop::SdlLvglHost host(options);
        host.setInputObserver(platform::desktop::ScreenInputRouter::observe, &router);
        host.setKeyObserver([](void*, uint32_t value, bool)
                            { if (value) ++keys; return false; },
                            nullptr);
        host.setTextObserver([](void*)
                             { return true; },
                             [](void*, const char*)
                             { ++commits; },
                             nullptr);
        host.pump();
        clock = 9000;
        key(host, SDLK_x, true);
        key(host, SDLK_x, false);
        require(keys == 2, "awake keys not delivered");
        clock = 18999;
        service_screen_runtime();
        require(!screen::is_sleeping(), "activity did not extend deadline");
        clock = 19000;
        service_screen_runtime();
        require(screen::is_sleeping(), "timeout did not enter sleep");
        await([&]
              { return !power->snapshot().powered; });
        key(host, SDLK_x, true);
        key(host, SDLK_x, false);
        require(screen::is_saver_active() && shown == 1 && presented == 1 && keys == 2, "wake key reached underlying page");
        SDL_Event text{};
        text.type = SDL_TEXTINPUT;
        text.text.text[0] = 'x';
        require(SDL_PushEvent(&text) == 1 && host.pump() && commits == 0, "wake text reached editor");
        key(host, SDLK_SPACE, true);
        key(host, SDLK_SPACE, true); // Repeat must remain quarantined after resume.
        key(host, SDLK_SPACE, false);
        require(!screen::is_saver_active() && !screen::is_sleeping() && keys == 2, "confirm/repeat reached page");
        await([&]
              { return power->snapshot().powered; });
        SDL_PushEvent(&text);
        host.pump();
        require(commits == 1, "awake editor lost input");

        std::thread first([]
                          { for (unsigned i = 0; i < 1000; ++i) screen::disable_sleep(); });
        std::thread second([]
                           { for (unsigned i = 0; i < 1000; ++i) screen::disable_sleep(); });
        first.join();
        second.join();
        clock = 600000;
        service_screen_runtime();
        require(screen::is_sleep_disabled() && !screen::is_sleeping(), "nested inhibition lost");
        for (unsigned i = 0; i < 1999; ++i) screen::enable_sleep();
        service_screen_runtime();
        require(screen::is_sleep_disabled(), "inhibition released too early");
        screen::enable_sleep();
        require(!screen::is_sleep_disabled(), "final inhibition not released");
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.block_off = true;
        }
        clock = 610000;
        const auto wakes_before_race = compositor.wakes();
        service_screen_runtime();
        {
            std::unique_lock<std::mutex> lock(compositor.mutex);
            require(compositor.changed.wait_for(lock, std::chrono::seconds(3), [&]
                                                { return compositor.entered; }),
                    "off barrier not reached");
        }
        key(host, SDLK_x, true);
        key(host, SDLK_x, false);
        require(screen::is_saver_active(), "fast wake failed");
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.block_off = false;
            compositor.changed.notify_all();
        }
        await([&]
              { service_screen_runtime(); return compositor.wakes() > wakes_before_race && compositor.powered() && power->snapshot().powered; });
        clock = 613000;
        service_screen_runtime();
        await([&]
              { return !power->snapshot().powered; });
        pointer(host, 200, true);
        pointer(host, 200, false);
        require(screen::is_saver_active(), "first touch dismissed preview");
        pointer(host, 200, true);
        pointer(host, 100, false);
        require(!screen::is_saver_active() && !screen::is_sleeping(), "upward swipe did not resume");
        await([&]
              { return power->snapshot().powered; });
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.ignore_off = true;
        }
        clock = 623000;
        service_screen_runtime();
        await([&]
              { return power->snapshot().failed; });
        service_screen_runtime();
        require(!screen::is_sleeping() && compositor.powered(), "failed power operation stranded UI");
        await([&]
              { service_screen_runtime(); return !power->snapshot().failed; });
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.ignore_off = false;
        }
        clock = 633000;
        service_screen_runtime();
        await([&]
              { return !power->snapshot().powered; });
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.available = false;
        }
        await([&]
              { return !power->snapshot().available; });
        service_screen_runtime();
        require(!screen::is_sleeping() && !screen::supports_app_timeout_setting(), "missing output did not release sleep");
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.available = true;
        }
        await([&]
              { service_screen_runtime(); return power->snapshot().available && power->snapshot().powered; });
        clock = 643000;
        service_screen_runtime();
        await([&]
              { return !power->snapshot().powered; });
        shutdown_screen_runtime();
        power.reset();
        require(compositor.powered() && !screen::supports_app_timeout_setting(), "shutdown failed restoration");
        require(menus == 0, "resume replaced active page with menu");
    }
    lv_deinit();
    std::filesystem::remove_all(root);
}
