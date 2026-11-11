#include "app/input_event.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "ui/app_runtime.h"
#include "ui/components/bottom_bar.h"
#include "ui/input/cardputer_keyboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

using Key = trailmate::cardputer_zero::app::InputKey;
using namespace std::chrono_literals;

static lv_obj_t* visible_label(lv_obj_t* object, const char* text)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = visible_label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() / ("trailmate-softkeys-" + std::to_string(getpid()));
    std::filesystem::create_directories(directory);
    setenv("TRAIL_MATE_SETTINGS_ROOT", directory.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", directory.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (directory / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
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
        const auto key = [&](char value)
        {
            shell.enqueueInputs({{Key::Character, std::string(1, value), value}});
            tick(200ms);
        };
        tick(3300ms);
        assert(!ui_get_active_app());
        char node_id[12]{};
        std::snprintf(node_id, sizeof(node_id), "!%08lX", static_cast<unsigned long>(shell.facade().getSelfNodeId()));
        assert(!visible_label(lv_screen_active(), node_id));
        key('8');
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "settings"));
        key('8');
        auto* device = visible_label(lv_screen_active(), "Device");
        assert(device);
        lv_obj_send_event(lv_obj_get_parent(device), LV_EVENT_CLICKED, nullptr);
        tick(200ms);
        assert(visible_label(lv_screen_active(), "Node ID"));
        assert(visible_label(lv_screen_active(), node_id));
        shell.enqueueInputs({{Key::Power}});
        tick(500ms);
        assert(!ui_get_active_app());
        key('4');
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "chat"));
        key('8');
        assert(visible_label(lv_screen_active(), "Info"));
        key('m');
        assert(!visible_label(lv_screen_active(), "Info"));
        key('m');
        assert(visible_label(lv_screen_active(), "Info"));
        key('8');
        assert(!visible_label(lv_screen_active(), "Info"));
        shell.enqueueInputs({{Key::Power}});
        tick(500ms);
        assert(!ui_get_active_app());

        {
            auto edit = shell.facade().beginConfigEdit();
            assert(edit);
            edit.config().mesh_protocol = chat::MeshProtocol::Meshtastic;
            edit.commit(app::AppConfigChangeSet::mesh());
            shell.facade().applyMeshConfig();
        }
        team::ui::TeamUiSnapshot membership;
        membership.in_team = membership.has_team_id = membership.has_team_psk = true;
        membership.team_id = {1, 2, 3, 4, 5, 6, 7, 8};
        membership.team_name = "Softkey Team";
        membership.security_round = 1;
        team::ui::team_ui_snapshot_store().save(membership);
        auto* team = visible_label(lv_screen_active(), "Team");
        assert(team);
        lv_obj_send_event(lv_obj_get_parent(team), LV_EVENT_CLICKED, nullptr);
        tick(200ms);
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "team"));
        auto* roster = visible_label(lv_screen_active(), "Roster");
        assert(roster);
        assert(lv_obj_get_height(roster) <= lv_obj_get_content_height(lv_obj_get_parent(roster)));
        key('4');
        assert(visible_label(lv_screen_active(), "Team / Member"));
        shell.enqueueInputs({{Key::Power}});
        tick(200ms);
        assert(visible_label(lv_screen_active(), "Roster"));
        shell.enqueueInputs({{Key::Power}});
        tick(500ms);
        assert(!ui_get_active_app());
        key('5');
        tick(300ms);
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "map"));
        key('8');
        assert(visible_label(lv_screen_active(), "Search"));
        key('m');
        assert(!visible_label(lv_screen_active(), "Search"));
        shell.enqueueInputs({{Key::Power}});
        tick(500ms);
        team::ui::team_ui_snapshot_store().clear();
        assert(!ui_get_active_app());

        // Check the physical positions independently of the page's actions.
        auto* page = lv_obj_create(lv_screen_active());
        lv_obj_set_size(page, 320, 170);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        auto* bar = ui::components::bottom_bar::create(page);
        auto* group = lv_group_create();
        unsigned clicks[5]{};
        lv_obj_t* buttons[5]{};
        for (unsigned slot = 0; slot < 5; ++slot)
        {
            buttons[slot] = ui::components::bottom_bar::add_button(
                bar, slot == 4 ? "Menu" : "Action",
                [](lv_event_t* event)
                { ++*static_cast<unsigned*>(lv_event_get_user_data(event)); },
                &clicks[slot]);
            lv_group_add_obj(group, buttons[slot]);
        }
        lv_obj_update_layout(page);
        lv_area_t bounds{};
        lv_obj_get_coords(bar, &bounds);
        for (unsigned slot = 0; slot < 5; ++slot)
        {
            lv_area_t button{};
            lv_obj_get_coords(buttons[slot], &button);
            const auto centre = (button.x1 + button.x2 + 1) / 2 - bounds.x1;
            assert(std::abs(centre - static_cast<int>(48 + slot * 60)) <= 1);
            lv_group_focus_obj(buttons[0]);
            ui::input::cardputer::State state;
            uint32_t physical = '4' + slot;
            assert(ui::input::cardputer::handle_key(physical, true, group, state));
            assert(clicks[slot] == 1);
        }
        // Map's initial Back focus is a sibling of the viewport/footer. The
        // footer still belongs to the active group and must accept softkeys.
        auto* sibling_back = lv_btn_create(lv_screen_active());
        lv_group_add_obj(group, sibling_back);
        lv_group_focus_obj(sibling_back);
        uint32_t sibling_key = '7';
        ui::input::cardputer::State sibling_state;
        assert(ui::input::cardputer::handle_key(sibling_key, true, group, sibling_state));
        assert(clicks[3] == 2);
        auto* editor = lv_textarea_create(page);
        lv_group_add_obj(group, editor);
        lv_group_focus_obj(editor);
        for (uint32_t digit = '4'; digit <= '8'; ++digit)
        {
            ui::input::cardputer::State state;
            auto input = digit;
            assert(!ui::input::cardputer::handle_key(input, true, group, state));
            assert(input == digit);
        }
        lv_group_delete(group);
        lv_obj_delete(sibling_back);
        lv_obj_delete(page); // Registrations must leave with their widgets.
    }
    std::filesystem::remove_all(directory);
}
