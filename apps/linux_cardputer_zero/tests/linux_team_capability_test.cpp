#include "app/input_event.h"
#include "team/usecase/team_service.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/shell_ui_runner.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
using Key = trailmate::cardputer_zero::app::InputKey;

lv_obj_t* find_label(lv_obj_t* object, const char* text)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = find_label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const bool demo = !std::strcmp(argv[1], "demo");
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-team-capability-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", argv[1], 1);
    unsetenv("TRAIL_MATE_DEMO_WORLD");
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    {
        trailmate::cardputer_zero::linux_ui::ShellSession shell;
        trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        assert((shell.facade().getTeamPairing() != nullptr) == demo);
        assert(shell.facade().getTeamController() && shell.facade().getTeamService());
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
            tick(200ms);
        };
        tick(3300ms);
        key(Key::Character, 'x');
        assert(find_label(lv_group_get_focused(menu_g), "Team"));
        key(Key::Enter);
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "team"));
        auto* create_label = find_label(lv_screen_active(), "Create Team");
        auto* join_label = find_label(lv_screen_active(), "Join Team");
        assert(create_label && join_label);
        auto* create = lv_obj_get_parent(create_label);
        auto* join = lv_obj_get_parent(join_label);
        assert(lv_obj_has_state(create, LV_STATE_DISABLED) == !demo);
        assert(lv_obj_has_state(join, LV_STATE_DISABLED) == !demo);
        assert((find_label(lv_screen_active(), "Team pairing is unavailable on this device") != nullptr) == !demo);
        if (!demo)
        {
            assert(!shell.facade().getTeamService()->hasKeys());
            // A direct stale/programmatic event must not create local team keys either.
            lv_obj_send_event(create, LV_EVENT_CLICKED, nullptr);
            lv_obj_send_event(join, LV_EVENT_CLICKED, nullptr);
            tick(250ms);
            assert(!shell.facade().getTeamService()->hasKeys());
            assert(find_label(lv_screen_active(), "You are not in a team"));
            key(Key::Tab);
            key(Key::Down);
        }
        key(Key::Power);
        tick(500ms);
        assert(!ui_get_active_app());
    }
    std::filesystem::remove_all(root);
}
