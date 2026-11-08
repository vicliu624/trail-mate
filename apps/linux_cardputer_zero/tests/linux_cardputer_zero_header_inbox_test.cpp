#include "ui/screens/chat/chat_message_list_components.h"
#include "ui/shell_ui_runner.h"
#include "ui/widgets/top_bar.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

static lv_obj_t* find_label(lv_obj_t* object, const char* text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        if (auto* found = find_label(lv_obj_get_child(object, i), text)) return found;
    return nullptr;
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-header-inbox-" + std::to_string(getpid()));
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
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(3300);
        while (std::chrono::steady_clock::now() < end)
        {
            host.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        {
            auto* page = lv_obj_create(lv_screen_active());
            lv_obj_set_size(page, 320, 170);
            lv_obj_set_style_pad_all(page, 0, 0);
            lv_obj_set_style_border_width(page, 0, 0);
            ui::widgets::TopBar bar;
            ui::widgets::TopBarConfig config;
            config.power_indicator = false;
            ui::widgets::top_bar_init(bar, page, config);
            for (const char* title : {"[MT] Trail Mate T-Display-CM0 with a long name",
                                      "[MC] Cardputer Zero with a long peer name",
                                      "[RT] Long destination name", "[MT] 中文节点的较长名称"})
            {
                ui::widgets::top_bar_set_title(bar, title);
                for (const char* status : {LV_SYMBOL_BATTERY_FULL " 100%", LV_SYMBOL_CHARGE " 100%", "USB", "18:56"})
                {
                    ui::widgets::top_bar_set_right_text_ascii(bar, status);
                    lv_obj_update_layout(page);
                    assert(lv_label_get_long_mode(bar.title_label) == LV_LABEL_LONG_DOT);
                    const auto* font = lv_obj_get_style_text_font(bar.title_label, LV_PART_MAIN);
                    assert(lv_obj_get_height(bar.title_label) <= lv_font_get_line_height(font));
                    lv_point_t required;
                    lv_text_get_size(&required, status, lv_obj_get_style_text_font(bar.right_label, LV_PART_MAIN),
                                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                    assert(required.x <= lv_obj_get_content_width(bar.right_label));
                    lv_area_t container, title_area, right, back;
                    lv_obj_get_coords(bar.container, &container);
                    lv_obj_get_coords(bar.title_label, &title_area);
                    lv_obj_get_coords(bar.right_label, &right);
                    lv_obj_get_coords(bar.back_btn, &back);
                    assert(title_area.x1 > back.x2 && title_area.x2 < right.x1);
                    assert(title_area.y1 >= container.y1 && title_area.y2 <= container.y2);
                    assert(right.x2 < 320 && right.y1 >= container.y1 && right.y2 <= container.y2);
                }
            }
            lv_obj_delete(page);
        }
        {
            chat::ui::ChatMessageListScreen inbox(lv_screen_active());
            chat::ConversationMeta direct;
            direct.id = {chat::ChannelId::PRIMARY, 123, chat::MeshProtocol::Meshtastic};
            direct.name = "Private peer";
            direct.preview = "old direct";
            direct.last_timestamp = 100;
            inbox.setConversations({direct});
            assert(inbox.getItemCount() == 1);
            assert(find_label(inbox.getDirectButton(), "All"));
            inbox.setSelectedConversation(direct.id);
            auto* same_screen = inbox.getObj();
            chat::ConversationMeta broadcast;
            broadcast.id = {chat::ChannelId::PRIMARY, 0, chat::MeshProtocol::Meshtastic};
            broadcast.name = "Broadcast";
            broadcast.preview = "official live hi";
            broadcast.last_timestamp = 200;
            broadcast.unread = 1;
            // Simulate the normal refresh while the existing inbox stays open.
            inbox.setConversations({broadcast, direct});
            lv_obj_update_layout(inbox.getObj());
            assert(inbox.getObj() == same_screen && inbox.getItemCount() == 2);
            assert(find_label(inbox.getObj(), "official live hi"));
            assert(find_label(inbox.getObj(), "1"));
            broadcast.preview = "new broadcast while open";
            broadcast.unread = 2;
            inbox.setConversations({broadcast, direct});
            assert(find_label(inbox.getObj(), "new broadcast while open"));
            assert(find_label(inbox.getObj(), "2"));
            lv_obj_send_event(inbox.getBroadcastButton(), LV_EVENT_CLICKED, nullptr);
            assert(inbox.getItemCount() == 1);
            lv_obj_send_event(inbox.getDirectButton(), LV_EVENT_CLICKED, nullptr);
            assert(inbox.getItemCount() == 2);
        }
    }
    std::filesystem::remove_all(root);
    std::puts("Zero header and inbox: single-line peer names, complete status, live broadcast insertion/preview/unread and All filter passed");
}
