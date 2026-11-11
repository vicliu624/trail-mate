#include "app/input_event.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_layout.h"
#include "ui/shell_ui_runner.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

// Explicit hardware-only executable. It is deliberately not a CTest test:
// running it changes wlan0 temporarily and requires the installed recovery helper.
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
    assert(argc == 3);
    const bool restore = !std::strcmp(argv[1], "restore");
    const bool leader = !std::strcmp(argv[1], "leader");
    assert(restore || leader || !std::strcmp(argv[1], "member"));
    const std::filesystem::path root = std::filesystem::absolute(argv[2]);
    assert(restore || !std::filesystem::exists(root / "team.snapshot"));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_RUNTIME_MODE", "mesh", 1);
    unsetenv("TRAIL_MATE_DEMO_WORLD");
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    {
        trailmate::cardputer_zero::linux_ui::ShellSession shell;
        trailmate::cardputer_zero::linux_ui::CanvasLvglHost host(shell);
        assert(shell.begin());
        assert(shell.facade().getTeamPairing());
        const auto tick = [&](auto duration)
        {
            const auto deadline = std::chrono::steady_clock::now() + duration;
            do
            {
                host.tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
        };
        const auto key = [&](Key code, char value = 0)
        {
            shell.enqueueInputs({{code, value ? std::string(1, value) : std::string{}, value}});
            tick(std::chrono::milliseconds(200));
        };
        tick(std::chrono::milliseconds(3300));
        key(Key::Character, 'x');
        assert(find_label(lv_group_get_focused(menu_g), "Team"));
        key(Key::Enter);
        assert(ui_get_active_app() && !std::strcmp(ui_get_active_app()->stable_id(), "team"));
        if (!restore)
        {
            auto* label = find_label(lv_screen_active(), leader ? "Create Team" : "Join Team");
            assert(label && !lv_obj_has_state(lv_obj_get_parent(label), LV_STATE_DISABLED));
            lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
            std::puts("REAL_UI_PAIRING_STARTED");
            std::fflush(stdout);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(leader ? 45 : 28);
            while (std::chrono::steady_clock::now() < deadline)
            {
                tick(std::chrono::milliseconds(50));
                team::ui::TeamUiSnapshot snapshot;
                if (team::ui::team_ui_snapshot_store().load(snapshot) && snapshot.in_team && snapshot.has_team_psk &&
                    (leader ? snapshot.members.size() >= 2 : !snapshot.pending_join)) break;
            }
            if (leader)
            {
                // The leader keeps enrollment open for additional members.
                // Use the real Cancel action to close enrollment while keeping
                // the team, then verify the resulting durable UI state.
                tick(std::chrono::milliseconds(300));
                if (auto* cancel = find_label(lv_screen_active(), "Cancel"))
                    lv_obj_send_event(lv_obj_get_parent(cancel), LV_EVENT_CLICKED, nullptr);
                tick(std::chrono::milliseconds(300));
            }
        }
        team::ui::TeamUiSnapshot snapshot;
        assert(team::ui::team_ui_snapshot_store().load(snapshot));
        assert(snapshot.in_team && snapshot.has_team_id && snapshot.has_team_psk && !snapshot.pending_join);
        assert(shell.facade().getTeamService()->hasKeys());
        if (!restore && leader) assert(snapshot.members.size() >= 2);
        std::array<uint8_t, 28> material{};
        std::copy(snapshot.team_id.begin(), snapshot.team_id.end(), material.begin());
        for (unsigned i = 0; i < 4; ++i) material[8 + i] = static_cast<uint8_t>(snapshot.security_round >> (8 * i));
        std::copy(snapshot.team_psk.begin(), snapshot.team_psk.end(), material.begin() + 12);
        const auto output = root / (restore ? "restored-material.bin" : "paired-material.bin");
        const int file = ::open(output.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
        assert(file >= 0);
        assert(::write(file, material.data(), material.size()) == static_cast<ssize_t>(material.size()));
        assert(::fsync(file) == 0);
        ::close(file);
        shell.facade().getTeamPairing()->stop();
        tick(std::chrono::milliseconds(200));
        key(Key::Power);
        tick(std::chrono::milliseconds(500));
        assert(!ui_get_active_app());
        std::printf("REAL_UI_TEAM_%s_SUCCESS members=%zu\n", restore ? "RESTORE" : "PAIRING", snapshot.members.size());
    }
}
