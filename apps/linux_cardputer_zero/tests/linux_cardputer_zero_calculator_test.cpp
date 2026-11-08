#include "app/input_event.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace trailmate::cardputer_zero;
namespace input = trailmate::cardputer_zero::app;
using namespace std::chrono_literals;
bool hasLabel(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (hasLabel(lv_obj_get_child(object, i), text)) return true;
    return false;
}
bool hasResult(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text) &&
        !lv_obj_check_type(lv_obj_get_parent(object), &lv_button_class)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (hasResult(lv_obj_get_child(object, i), text)) return true;
    return false;
}
void checkButtons(lv_obj_t* object)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(object, &lv_button_class))
    {
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 320 && area.y2 < 170);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) checkButtons(lv_obj_get_child(object, i));
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-compact-calculator-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "offline", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    {
        linux_ui::ShellSession shell;
        linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        const auto tick = [&](auto duration)
        {
            const auto end = std::chrono::steady_clock::now() + duration;
            do
            {
                host.tick();
                std::this_thread::sleep_for(5ms);
            } while (std::chrono::steady_clock::now() < end);
        };
        tick(3300ms);
        assert(shell.ready());
        const auto key = [&](input::InputKey code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(100ms);
        };
        for (int i = 0; i < 6; ++i) key(input::InputKey::Next);
        key(input::InputKey::Enter);
        tick(650ms);
        assert(hasLabel(lv_screen_active(), "CALCULATOR"));
        lv_obj_update_layout(lv_screen_active());
        checkButtons(lv_screen_active());
        key(input::InputKey::Character, '1');
        key(input::InputKey::Character, '2');
        key(input::InputKey::Character, '+');
        key(input::InputKey::Character, '3');
        key(input::InputKey::Left);
        key(input::InputKey::Character, '9');
        key(input::InputKey::Character, '=');
        assert(hasResult(lv_screen_active(), "105")); // 12 + 93, preserving expression cursor movement.
        const auto focused_control = []() -> lv_obj_t*
        {
            for (auto* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
                if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD)
                    return lv_group_get_focused(lv_indev_get_group(indev));
            return nullptr;
        };
        key(input::InputKey::Tab);
        assert(focused_control() && hasLabel(focused_control(), "8"));
        key(input::InputKey::Right);
        assert(focused_control() && hasLabel(focused_control(), "9"));
        key(input::InputKey::Enter);
        key(input::InputKey::Character, '=');
        assert(hasResult(lv_screen_active(), "9")); // Tab enters button navigation; Enter inputs 9; '=' evaluates it.
        key(input::InputKey::Home);
        tick(650ms);
        assert(!hasLabel(lv_screen_active(), "CALCULATOR"));
    } // Join application workers and close SQLite before removing their files.
    std::filesystem::remove_all(root);
}
