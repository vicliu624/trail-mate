#include "app/input_event.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
using Key = trailmate::cardputer_zero::app::InputKey;
lv_group_t* active_group()
{
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) return lv_indev_get_group(input);
    return nullptr;
}
lv_obj_t* visible_label(lv_obj_t* object, const char* text)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = visible_label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}
int main(int argc, char** argv)
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-keyboard-modal-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "sd/routes");
    std::ofstream(root / "sd/routes/keyboard-route.kml")
        << "<kml><Document><Placemark><name>Keyboard route</name><LineString>"
           "<coordinates>102.70,25.04,0 102.71,25.05,0</coordinates>"
           "</LineString></Placemark></Document></kml>";
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    {
        trailmate::cardputer_zero::linux_ui::ShellSession shell;
        trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
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
        const auto key = [&](Key code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(150ms);
        };
        const auto click = [&](const char* text)
        {
            auto* label = visible_label(lv_screen_active(), text);
            assert(label);
            auto* button = label;
            while (button && !lv_obj_get_group(button)) button = lv_obj_get_parent(button);
            if (!button || lv_obj_has_state(button, LV_STATE_DISABLED)) std::fprintf(stderr, "Unreachable control: %s\n", text);
            assert(button && !lv_obj_has_state(button, LV_STATE_DISABLED));
            for (unsigned steps = 0; steps < 50 && lv_group_get_focused(active_group()) != button; ++steps) key(Key::Tab);
            assert(lv_group_get_focused(active_group()) == button);
            key(Key::Enter);
        };
        const auto require_menu = [&](const char* phase)
        {
            if (ui_get_active_app() && ui_is_transition_pending())
            {
                // App exit is scheduled on LVGL's next timer iteration.
                // A costly map frame may use the short key-pump budget first.
                std::fprintf(stderr, "%s: awaiting requested app transition\n", phase);
                const auto deadline = std::chrono::steady_clock::now() + 2s;
                while (ui_get_active_app() && std::chrono::steady_clock::now() < deadline) tick(10ms);
            }
            if (ui_get_active_app())
            {
                std::fprintf(stderr, "%s exit left app=%s\n", phase, ui_get_active_app()->stable_id());
                const auto dump = [&](auto&& self, lv_obj_t* object) -> void
                {
                    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
                    if (lv_obj_check_type(object, &lv_label_class)) std::fprintf(stderr, "visible: %s\n", lv_label_get_text(object));
                    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) self(self, lv_obj_get_child(object, i));
                };
                dump(dump, lv_screen_active());
            }
            assert(!ui_get_active_app());
        };
        tick(3300ms);
        auto* initial = lv_group_get_focused(active_group());
        key(Key::Character, 'c');
        assert(lv_group_get_focused(active_group()) != initial);
        key(Key::Character, 'z');
        assert(lv_group_get_focused(active_group()) == initial);
        key(Key::Character, 'x');
        auto* below = lv_group_get_focused(active_group());
        assert(below != initial && visible_label(below, "Team"));
        key(Key::Character, 'f');
        assert(lv_group_get_focused(active_group()) == initial);
        key(Key::Character, '7');
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "contacts"));
        tick(400ms);
        key(Key::F1);
        assert(visible_label(lv_screen_active(), "Contacts Help"));
        key(Key::Power);
        assert(!visible_label(lv_screen_active(), "Contacts Help"));
        key(Key::Character, 'm');
        key(Key::Power);
        key(Key::Power);
        require_menu("Contacts");

        key(Key::Character, '6');
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "tracker"));
        tick(1600ms);
        key(Key::Character, '6'); // Route selection uses the existing page action.
        tick(500ms);
        key(Key::Character, '7');
        auto* load = lv_group_get_focused(active_group());
        assert(visible_label(load, "Load"));
        key(Key::Down);
        auto* preview = lv_group_get_focused(active_group());
        assert(preview != load && visible_label(preview, "Preview"));
        key(Key::Right);
        assert(visible_label(lv_group_get_focused(active_group()), "Delete"));
        key(Key::Left);
        assert(lv_group_get_focused(active_group()) == preview);
        key(Key::Up);
        assert(lv_group_get_focused(active_group()) == load);
        key(Key::Power);
        key(Key::Power);
        require_menu("Tracker");

        key(Key::Character, '5');
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "map"));
        tick(700ms);
        key(Key::F1); // The physical Fn+H key must reach Map's detailed help.
        assert(visible_label(lv_screen_active(), "Map Help"));
        key(Key::Power);
        assert(!visible_label(lv_screen_active(), "Map Help"));
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "map"));
        if (argc > 1 && !std::strcmp(argv[1], "--shutdown-active-map"))
        {
            key(Key::Character, '8');
            key(Key::Down);
            key(Key::Down);
            key(Key::Down);
            key(Key::Enter);
            assert(visible_label(lv_screen_active(), "Navigation"));
            shell.releaseUiBindings();
            assert(!ui_get_active_app() && !ui_is_transition_pending());
            shell.releaseUiBindings(); // The host and session both release bindings.
        }
        else
        {
            key(Key::Character, 'm');
            auto* contours = lv_group_get_focused(active_group());
            key(Key::Down);
            auto* layer = lv_group_get_focused(active_group());
            assert(layer != contours);
            key(Key::Right); // Enter the Map Layer submenu; parent widgets are rebuilt.
            assert(visible_label(lv_screen_active(), "Terrain"));
            key(Key::Power); // Restore the parent menu and its Map Layer focus.
            key(Key::Up);
            assert(visible_label(lv_group_get_focused(active_group()), "Contours"));
            key(Key::Power);
            key(Key::Power);
            require_menu("Map");

            key(Key::Character, '8');
            tick(250ms); // Let the dashboard feed transition finish.
            assert(!ui_get_active_app() && ui::menu::dashboard::isFeedOpen());
            key(Key::Character, '8');
            tick(250ms);
            assert(!ui::menu::dashboard::isFeedOpen());
            require_menu("Widgets");
            assert(ui::menu_layout::launchAppByStableId("settings"));
            assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "settings"));
            tick(400ms);
            click("Protocol");
            assert(visible_label(lv_group_get_focused(active_group()), "Meshtastic"));
            key(Key::Down);
            assert(visible_label(lv_group_get_focused(active_group()), "MeshCore"));
            key(Key::Right);
            assert(visible_label(lv_group_get_focused(active_group()), "Reticulum"));
            key(Key::Power);
            key(Key::Power);
            require_menu("Settings");
        }
    }
    std::filesystem::remove_all(root);
}
