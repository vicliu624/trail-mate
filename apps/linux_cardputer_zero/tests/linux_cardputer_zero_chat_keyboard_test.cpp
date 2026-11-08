#include "app/input_event.h"
#include "ui/menu/menu_runtime.h"
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/chat/chat_conversation_layout.h"
#include "ui/shell_ui_runner.h"
#include "ui/widgets/ime/ime_widget.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
using Key = trailmate::cardputer_zero::app::InputKey;

static lv_obj_t* label(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index)
        if (auto* found = label(lv_obj_get_child(object, index), text)) return found;
    return nullptr;
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-chat-keyboard-" + std::to_string(getpid()));
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
            tick(120ms);
        };
        tick(3300ms);
        auto* keypad = lv_indev_get_next(nullptr);
        while (keypad && lv_indev_get_type(keypad) != LV_INDEV_TYPE_KEYPAD) keypad = lv_indev_get_next(keypad);
        assert(keypad);
        auto* previous = lv_group_get_default();
        auto* previous_input = lv_indev_get_group(keypad);
        auto* group = lv_group_create();
        lv_group_set_default(group);
        lv_indev_set_group(keypad, group);
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::App);
        {
            chat::ui::ChatComposeScreen compose(lv_screen_active(), {chat::ChannelId::PRIMARY, 123, chat::MeshProtocol::Meshtastic});
            ui::widgets::ImeWidget ime;
            ime.init(compose.getContent(), compose.getTextarea());
            compose.attachImeWidget(&ime);
            compose.setVoiceButton("Voice", true);
            unsigned sends = 0;
            compose.setActionCallback([](chat::ui::ChatComposeScreen::ActionIntent intent, void* context)
                                      {
                assert(intent == chat::ui::ChatComposeScreen::ActionIntent::Send);
                ++*static_cast<unsigned*>(context); },
                                      &sends);
            tick(150ms);
            assert(lv_group_get_focused(group) == compose.getTextarea());
            for (char value : std::string("hwsv")) key(Key::Character, value);
            assert(compose.getText() == "hwsv");
            key(Key::Enter);
            assert(compose.getText() == "hwsv\n" && sends == 0);
            key(Key::Character, 'x');
            auto* send = lv_obj_get_parent(label(compose.getActionBar(), "Send"));
            assert(send);
            key(Key::Tab);
            assert(lv_group_get_focused(group) == send);
            assert(compose.getText() == "hwsv\nx" && sends == 0);
            key(Key::Enter);
            if (sends != 1) std::fprintf(stderr, "Send actions for one key press: %u\n", sends);
            assert(sends == 1); // Key press/release must not duplicate the send.
            assert(label(compose.getObj(), "Tab to Send, then Enter"));
            lv_obj_update_layout(compose.getObj());
            assert(lv_obj_get_height(send) == 20);
            assert(lv_obj_get_height(compose.getActionBar()) == 26);
            for (uint32_t index = 0; index < lv_obj_get_child_count(compose.getActionBar()); ++index)
            {
                auto* child = lv_obj_get_child(compose.getActionBar(), index);
                if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
                lv_area_t area;
                lv_obj_get_coords(child, &area);
                assert(area.x1 >= 0 && area.x2 < 320 && area.y1 >= 0 && area.y2 < 170);
            }
            std::set<lv_obj_t*> visited{lv_group_get_focused(group)};
            for (uint32_t index = 0; index < lv_group_get_obj_count(group); ++index)
            {
                key(Key::Tab);
                visited.insert(lv_group_get_focused(group));
            }
            assert(visited.count(ime.toggle_btn()) && visited.count(compose.getTextarea()));
            ime.detach();
        }
        {
            auto conversation = chat::ui::layout::create_conversation_base(lv_screen_active());
            lv_obj_update_layout(conversation.root);
            assert(lv_obj_get_height(conversation.action_bar) == 24);
            assert(lv_obj_get_height(conversation.reply_btn) == 20);
            assert(lv_obj_get_width(conversation.reply_btn) == 54);
            lv_obj_delete(conversation.root);
        }
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
        lv_group_set_default(previous);
        lv_indev_set_group(keypad, previous_input);
        lv_group_delete(group);
    }
    std::filesystem::remove_all(root);
}
