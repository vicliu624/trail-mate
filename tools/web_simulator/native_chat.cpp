// Preview data and application navigation around the unchanged firmware chat views.
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/chat/chat_message_list_components.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"
#include <memory>
#include <string>
#include <vector>
extern "C" void native_open_page(int page);
static std::unique_ptr<chat::ui::ChatMessageListScreen> message_list;
static std::unique_ptr<chat::ui::ChatConversationScreen> conversation;
static std::unique_ptr<chat::ui::ChatComposeScreen> compose;
static chat::ConversationId selected(chat::ChannelId::PRIMARY, 0x12345678);
static std::vector<std::string> sent_messages;
static int requested_page = 5;

void preview_chat_exit()
{
    compose.reset();
    conversation.reset();
    message_list.reset();
}
namespace chat::ui::runtime
{
bool requestCompose(const chat::ConversationId& id)
{
    selected = id;
    requested_page = 7;
    return true;
}
bool requestConversation(const chat::ConversationId& id)
{
    selected = id;
    requested_page = 6;
    return true;
}
void clearRequestedCompose() { requested_page = 5; }
} // namespace chat::ui::runtime
namespace chat::ui::shell
{
void enter(void*, lv_obj_t*)
{
    const int page = requested_page;
    requested_page = 5;
    native_open_page(page);
}
void exit(void*, lv_obj_t*) { preview_chat_exit(); }
lv_obj_t* get_container()
{
    if (::compose) return ::compose->getObj();
    if (::conversation) return ::conversation->getObj();
    if (::message_list) return ::message_list->getObj();
    return nullptr;
}
} // namespace chat::ui::shell
void preview_chat_enter(int page)
{
    if (page == 5)
    {
        message_list = std::make_unique<chat::ui::ChatMessageListScreen>(lv_screen_active());
        std::vector<chat::ConversationMeta> rows(2);
        rows[0].id = selected;
        rows[0].name = "Alex";
        rows[0].preview = "Meet at the ridge shelter?";
        rows[0].unread = 2;
        rows[0].last_timestamp = 1789110000;
        rows[1].id = chat::ConversationId(chat::ChannelId::PRIMARY);
        rows[1].name = "Primary";
        rows[1].preview = "Trail conditions are clear.";
        rows[1].last_timestamp = 1789109000;
        message_list->setConversations(rows);
        message_list->setActionCallback([](auto intent, const chat::ConversationId& id, void*)
                                        {
            if(intent==chat::ui::ChatMessageListScreen::ActionIntent::SelectConversation){selected=id;native_open_page(6);}
            if(intent==chat::ui::ChatMessageListScreen::ActionIntent::Back)native_open_page(10); },
                                        nullptr);
    }
    else if (page == 6)
    {
        conversation = std::make_unique<chat::ui::ChatConversationScreen>(lv_screen_active(), selected);
        conversation->setHeaderText(selected.peer ? "Alex" : "Primary");
        ::ui::chat::MessageRow row;
        row.ref.protocol_id = 1;
        row.sender_node_id = 0x12345678;
        ::ui::copyText(row.text, "Meet at the ridge shelter?");
        ::ui::copyText(row.time_label, "09:41");
        ::ui::copyText(row.sender_label, "Alex");
        row.delivery = ::ui::chat::MessageDeliveryState::Received;
        conversation->addMessage(row);
        for (const auto& text : sent_messages)
        {
            row.outgoing = true;
            row.ref.protocol_id++;
            row.delivery = ::ui::chat::MessageDeliveryState::Sent;
            ::ui::copyText(row.text, text.c_str());
            ::ui::copyText(row.sender_label, "You");
            conversation->addMessage(row);
        }
        conversation->setBackCallback([](void*)
                                      { native_open_page(5); },
                                      nullptr);
        conversation->setActionCallback([](auto intent, void*)
                                        {if(intent==chat::ui::ChatConversationScreen::ActionIntent::Reply)native_open_page(7); },
                                        nullptr);
    }
    else if (page == 7)
    {
        compose = std::make_unique<chat::ui::ChatComposeScreen>(lv_screen_active(), selected);
        compose->setHeaderText(selected.peer ? "Alex" : "Primary");
        compose->setBackCallback([](void*)
                                 { native_open_page(6); },
                                 nullptr);
        compose->setActionCallback([](auto intent, void*)
                                   {
            if(intent==chat::ui::ChatComposeScreen::ActionIntent::Send){sent_messages.push_back(compose->getText());native_open_page(6);}
            else if(intent==chat::ui::ChatComposeScreen::ActionIntent::Cancel)native_open_page(6); },
                                   nullptr);
    }
}
