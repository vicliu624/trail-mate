#include "app/app_facade_access.h"
#include "app/linux_app_config_blob.h"
#include "platform/ui/settings_store.h"
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/shell_ui_runner.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-config-chat-" + std::to_string(getpid()));
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
        auto tick = [&](unsigned milliseconds)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            do
            {
                host.tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < end);
        };
        tick(3300);
        auto& facade = app::appFacade();
        for (auto protocol : {chat::MeshProtocol::MeshCore, chat::MeshProtocol::Reticulum, chat::MeshProtocol::Meshtastic})
        {
            assert(facade.switchMeshProtocol(protocol, true));
            tick(600);
            chat::ui::ChatComposeScreen compose(lv_screen_active(), {chat::ChannelId::PRIMARY, 123, protocol});
            std::string text = "rt zero cm0 tm3 ";
            text.append(156, 'x');
            lv_textarea_set_text(compose.getTextarea(), text.c_str());
            tick(100);
            assert(facade.readConfig().mesh_protocol == protocol);
            settings::ui::shell::enter(nullptr, lv_screen_active());
            tick(100);
            assert(facade.readConfig().mesh_protocol == protocol);
            settings::ui::shell::exit(nullptr, lv_screen_active());
            facade.saveConfig();
            tick(700);
            trailmate::linux_app::LinuxAppConfigBlob blob;
            size_t length = 0;
            assert(platform::ui::settings_store::get_blob_into("linux_app_facade", "app_config_v1", &blob, sizeof(blob), &length));
            assert(length == sizeof(blob) && blob.config.mesh_protocol == protocol);
            assert(std::memcmp(&blob.config, &facade.readConfig(), sizeof(app::AppConfig)) == 0);
        }
    }
    std::filesystem::remove_all(root);
}
