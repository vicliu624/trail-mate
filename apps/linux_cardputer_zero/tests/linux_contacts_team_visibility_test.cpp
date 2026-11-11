#include "platform/ui/team_ui_snapshot_store.h"
#include "ui/screens/contacts/contacts_page_components.h"
#include "ui/screens/contacts/contacts_page_runtime.h"
#include "ui/screens/contacts/contacts_state.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unistd.h>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-contacts-team-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "local", 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    {
        trailmate::cardputer_zero::linux_ui::ShellSession shell;
        trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        {
            auto edit = shell.facade().beginConfigEdit();
            assert(edit);
            edit.config().mesh_protocol = chat::MeshProtocol::Meshtastic;
            edit.commit(app::AppConfigChangeSet::mesh());
            shell.facade().applyMeshConfig();
        }
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(3300);
        while (std::chrono::steady_clock::now() < end)
        {
            host.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        auto& store = team::ui::team_ui_snapshot_store();
        store.clear();
        contacts::ui::runtime::enter(nullptr, lv_screen_active());
        auto& state = contacts::ui::g_contacts_state;
        assert(contacts::ui::uses_node_card());
        assert(state.node_card && state.team_btn);
        assert(lv_obj_has_flag(state.team_btn, LV_OBJ_FLAG_HIDDEN));

        team::ui::TeamUiSnapshot snapshot;
        snapshot.has_team_id = snapshot.has_team_psk = snapshot.in_team = true;
        snapshot.team_id = {1, 2, 3, 4, 5, 6, 7, 8};
        snapshot.team_name = "Visibility test";
        store.save(snapshot);
        for (const auto mode : {contacts::ui::ContactsMode::Contacts, contacts::ui::ContactsMode::Nearby,
                                contacts::ui::ContactsMode::Ignored})
        {
            state.current_mode = mode;
            refresh_ui();
            assert(contacts::ui::uses_node_card());
            assert(!lv_obj_has_flag(state.team_btn, LV_OBJ_FLAG_HIDDEN));
        }
        assert(activate_contacts_filter(state.team_btn));
        assert(state.current_mode == contacts::ui::ContactsMode::Team);

        // Leaving while the Team filter is open must return to the node card.
        store.clear();
        refresh_ui();
        assert(state.current_mode == contacts::ui::ContactsMode::Contacts);
        assert(state.node_card && contacts::ui::uses_node_card());
        assert(lv_obj_has_flag(state.team_btn, LV_OBJ_FLAG_HIDDEN));
        contacts::ui::runtime::exit(lv_screen_active());

        // A previously persisted membership must be visible on first entry.
        store.save(snapshot);
        contacts::ui::runtime::enter(nullptr, lv_screen_active());
        assert(contacts::ui::uses_node_card());
        assert(!lv_obj_has_flag(state.team_btn, LV_OBJ_FLAG_HIDDEN));
        contacts::ui::runtime::exit(lv_screen_active());
    }
    std::filesystem::remove_all(root);
}
