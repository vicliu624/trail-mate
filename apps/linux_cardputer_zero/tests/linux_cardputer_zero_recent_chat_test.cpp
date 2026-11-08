#include "app/app_facade_access.h"
#include "app/input_event.h"
#include "chat/linux_sqlite_chat_store.h"
#include "chat/usecase/chat_service.h"
#include "ui/chat_ui_runtime.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/screens/chat/chat_page_runtime.h"
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

static lv_obj_t* label(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-recent-chat-" + std::to_string(getpid()));
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
            tick(250ms);
        };
        tick(3300ms);
        assert(app::configFacade().switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        key(Key::Character, 'v');
        key(Key::Down);
        key(Key::Down);
        auto* dashboard = ui::menu::dashboard::dashboard_state();
        assert(dashboard && dashboard->page == 2 && ui::menu::dashboard::isFeedOpen());
        key(Key::Enter); // Empty activity is inert.
        assert(!app::runtimeFacade().getChatUiRuntime());

        trailmate::linux_app::LinuxSqliteChatStore store;
        const chat::ConversationId first(chat::ChannelId::PRIMARY, 0x12345678, chat::MeshProtocol::Meshtastic);
        const chat::ConversationId second(chat::ChannelId::PRIMARY, 0x87654321, chat::MeshProtocol::Meshtastic);
        chat::ChatMessage message;
        message.from = message.peer = first.peer;
        message.msg_id = 101;
        message.timestamp = 101;
        message.text = "First DM";
        assert(store.appendDurably(message));
        message.from = message.peer = second.peer;
        message.msg_id = message.timestamp = 102;
        message.text = "Second DM";
        assert(store.appendDurably(message));
        auto& widget = dashboard->widgets[0];
        widget.refresh(widget.state);
        assert(label(widget.root, "Second DM"));
        assert(store.getUnread(first) == 1 && store.getUnread(second) == 1);
        // A/D selects the second row, leaving W/S available to the widget wheel.
        key(Key::Character, 'd');
        key(Key::Enter);
        auto* runtime = app::runtimeFacade().getChatUiRuntime();
        assert(runtime && runtime->getState() == chat::ui::ChatUiState::Conversation);
        assert(label(chat::ui::runtime::get_container(), "First DM"));
        assert(store.getUnread(first) == 0 && store.getUnread(second) == 1);
        key(Key::Home); // Conversation -> message list.
        key(Key::Home); // Message list -> launcher.
        key(Key::Character, 'v');
        dashboard = ui::menu::dashboard::dashboard_state();
        assert(ui::menu::dashboard::isFeedOpen() && dashboard->page == 2);
        assert(label(dashboard->widgets[0].root, "Second DM"));
        // The touch path opens the displayed row, also using normal read handling.
        auto* preview = label(dashboard->widgets[0].root, "Second DM");
        assert(preview);
        lv_obj_send_event(lv_obj_get_parent(preview), LV_EVENT_CLICKED, nullptr);
        tick(250ms);
        runtime = app::runtimeFacade().getChatUiRuntime();
        assert(runtime && runtime->getState() == chat::ui::ChatUiState::Conversation);
        assert(label(chat::ui::runtime::get_container(), "Second DM"));
        assert(store.getUnread(second) == 0);
        // Retire the active Chat page before destroying the test LVGL host.
        key(Key::Home);
        key(Key::Home);
        assert(!app::runtimeFacade().getChatUiRuntime());
        // A full Reticulum destination remains private when its latest sent
        // record uses numeric peer zero, including after reopening the widget.
        assert(app::configFacade().switchMeshProtocol(chat::MeshProtocol::Reticulum, false));
        message.protocol = chat::MeshProtocol::Reticulum;
        message.peer = 0;
        message.from = 0;
        message.msg_id = message.timestamp = 103;
        message.status = chat::MessageStatus::Failed;
        message.text = "RT private reply";
        message.reticulum_identity.valid = true;
        message.reticulum_identity.destination_hash[0] = 0x12;
        message.reticulum_identity.destination_hash[15] = 0xAB;
        assert(store.appendDurably(message));
        const auto summaries = app::messagingFacade().getChatService().getConversations(0, 3, nullptr);
        assert(summaries.size() == 1);
        assert(summaries.front().name == "RT 120000000000000000000000000000AB");
        assert(summaries.front().id.reticulum_identity.destination_hash[15] == 0xAB);
        key(Key::Character, 'v');
        key(Key::Enter);
        runtime = app::runtimeFacade().getChatUiRuntime();
        assert(runtime && runtime->getState() == chat::ui::ChatUiState::Conversation);
        assert(label(chat::ui::runtime::get_container(), "RT private reply"));
        key(Key::Home);
        key(Key::Home);
        assert(!app::runtimeFacade().getChatUiRuntime());
    }
    std::filesystem::remove_all(root);
}
