#include "app/input_event.h"
#include "platform/ui/orientation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/shell_ui_runner.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace trailmate::cardputer_zero;
using namespace std::chrono_literals;
using InputKey = trailmate::cardputer_zero::app::InputKey;

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("trailmate-compact-widgets-" + std::to_string(getpid()));
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
        const auto key = [&](InputKey code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(250ms);
        };
        tick(3300ms);
        assert(shell.ready());
        auto* initial = lv_group_get_focused(menu_g);
        assert(initial);
        key(InputKey::Right);
        auto* right = lv_group_get_focused(menu_g);
        assert(right && right != initial && lv_obj_get_y(right) == lv_obj_get_y(initial));
        key(InputKey::Down);
        auto* down = lv_group_get_focused(menu_g);
        assert(down && down != right && lv_obj_get_y(down) > lv_obj_get_y(right));
        key(InputKey::Character, 'v');
        assert(ui::menu::dashboard::isFeedOpen());
        auto* state = ui::menu::dashboard::dashboard_state();
        assert(state && state->widgets[0].root);
        const auto count = static_cast<unsigned>(ui::menu::dashboard::WidgetKind::Count) -
                           (::platform::ui::orientation::heading_supported() ? 0U : 1U);
        for (unsigned page = 0; page < count; ++page)
        {
            assert(state->page == page);
            auto* widget = state->widgets[0].root;
            assert(widget && lv_obj_is_valid(widget));
            assert(lv_obj_get_style_transform_scale_x(widget, LV_PART_MAIN) == 170);
            assert(lv_obj_get_width(widget) * 170 / 256 <= 320);
            assert(lv_obj_get_height(widget) * 170 / 256 <= lv_obj_get_height(state->grid));
            if (page + 1 < count) key(InputKey::Down);
        }
        // Widget controls take Left/Right without closing the feed.
        key(InputKey::Right);
        assert(ui::menu::dashboard::isFeedOpen());
        key(InputKey::Character, 'v');
        assert(!ui::menu::dashboard::isFeedOpen());
        assert(lv_group_get_focused(menu_g) == down);
        key(InputKey::Enter);
        assert(!ui::menu::dashboard::isFeedOpen());
        key(InputKey::Home);
        key(InputKey::Character, 'v');
        assert(ui::menu::dashboard::isFeedOpen());
        key(InputKey::Home);
        assert(!ui::menu::dashboard::isFeedOpen());
    }
    std::filesystem::remove_all(root);
}
