#include "app/input_event.h"
#include "platform/linux/walkie_service.h"
#include "ui/app_runtime.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace trailmate::cardputer_zero;
namespace cardputer_app = trailmate::cardputer_zero::app;
using namespace std::chrono_literals;

static bool has_label(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (has_label(lv_obj_get_child(object, i), text)) return true;
    return false;
}

static lv_obj_t* find_label(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = find_label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}

static void check_header(lv_obj_t* object)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), "H ?"))
    {
        auto* bar = lv_obj_get_parent(object);
        lv_obj_update_layout(bar);
        lv_coord_t right = -1;
        for (uint32_t i = 0; i < lv_obj_get_child_count(bar); ++i)
        {
            auto* child = lv_obj_get_child(bar, i);
            if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
            lv_area_t area{};
            lv_obj_get_coords(child, &area);
            assert(area.x1 > right && area.x2 < 320 && area.y1 >= 0 && area.y2 < 170);
            right = area.x2;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        check_header(lv_obj_get_child(object, i));
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const int index = std::atoi(argv[1]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-page-help-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_LORA_SIMULATED", "0", 1);
    setenv("TRAIL_MATE_GPS_DEMO_DEFAULTS", "0", 1);
    setenv("TRAIL_MATE_DEMO_WORLD", "0", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    {
        linux_ui::ShellSession shell;
        linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        auto tick = [&](auto duration)
        {
            const auto end = std::chrono::steady_clock::now() + duration;
            do
            {
                host.tick();
                std::this_thread::sleep_for(5ms);
            } while (std::chrono::steady_clock::now() < end);
        };
        auto key = [&](cardputer_app::InputKey code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(150ms);
        };
        tick(3300ms);
        for (int i = 0; i < index; ++i) key(cardputer_app::InputKey::Right);
        key(cardputer_app::InputKey::Enter);
        tick(650ms);
        assert(ui_get_active_app());
        assert(::platform::linux_runtime::native_walkie_built());
        check_header(lv_screen_active());
        assert(!has_label(lv_screen_active(), "Protocol Probe is unavailable on this target."));
        assert(!has_label(lv_screen_active(), "Walkie Talkie is unavailable on this target."));
        auto* keypad = lv_indev_get_next(nullptr);
        while (keypad && lv_indev_get_type(keypad) != LV_INDEV_TYPE_KEYPAD) keypad = lv_indev_get_next(keypad);
        assert(keypad);
        auto* previous = lv_indev_get_group(keypad);
        assert(previous);
        auto* focused = lv_group_get_focused(previous);
        const auto* id = ui_get_active_app()->stable_id();
        if (!std::strcmp(id, "walkie_talkie"))
        {
            auto* hint = find_label(lv_screen_active(), "T: Talk / listen");
            assert(hint);
            auto* stack = lv_obj_get_parent(hint);
            lv_obj_update_layout(stack);
            auto* meters = lv_obj_get_child(stack, lv_obj_get_child_count(stack) - 1);
            lv_area_t area{};
            lv_obj_get_coords(meters, &area);
            assert(area.y1 >= 24 && area.y2 < 170);
        }
        key(cardputer_app::InputKey::Character, 'h');
        const bool map_help = !std::strcmp(id, "map");
        assert(map_help ? has_label(lv_screen_active(), "Map Help") && lv_group_get_focused(previous) != focused
                        : lv_indev_get_group(keypad) != previous);
        if (!std::strcmp(id, "energy_sweep")) assert(has_label(lv_screen_active(), "RADIO UNAVAILABLE"));
        if (!std::strcmp(id, "chat")) assert(has_label(lv_screen_active(), "Search conversations"));
        if (!std::strcmp(id, "calculator")) assert(has_label(lv_screen_active(), "sin / cos / tan"));
        if (!std::strcmp(id, "energy_sweep")) assert(has_label(lv_screen_active(), "Select observed profile"));
        key(cardputer_app::InputKey::Down);
        key(cardputer_app::InputKey::Character, 'h');
        assert(lv_indev_get_group(keypad) == previous);
        assert(lv_group_get_focused(previous) == focused);
        auto* hint = find_label(lv_screen_active(), "H ?");
        assert(hint && lv_obj_has_flag(hint, LV_OBJ_FLAG_CLICKABLE));
        lv_obj_send_event(hint, LV_EVENT_CLICKED, nullptr);
        tick(150ms);
        assert(map_help ? has_label(lv_screen_active(), "Map Help") && lv_group_get_focused(previous) != focused
                        : lv_indev_get_group(keypad) != previous);
        key(cardputer_app::InputKey::Character, 'h');
        assert(lv_indev_get_group(keypad) == previous);
        assert(lv_group_get_focused(previous) == focused);
        key(cardputer_app::InputKey::Home);
        tick(650ms);
        assert(!ui_get_active_app());
    }
    std::filesystem::remove_all(root);
}
