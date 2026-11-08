#pragma once

#include "chat/usecase/auto_reply_policy.h"
#include "chat/usecase/chat_service.h"
#include <cctype>
#include <string>

namespace chat::auto_reply
{
struct ObserverHooks
{
    bool (*read_reply_text)(std::string&) = nullptr;
    bool (*screen_sleeping)() = nullptr;
    NodeId (*self_node)() = nullptr;
    size_t max_text_bytes = 0;
};

class Observer final : public ChatService::IncomingMessageObserver
{
  public:
    Observer(ChatService& service, ObserverHooks hooks) : service_(service), hooks_(hooks)
    {
        service_.addIncomingMessageObserver(this);
    }
    ~Observer() override { service_.removeIncomingMessageObserver(this); }

    void onIncomingMessage(const ChatMessage& message, const RxMeta*) override
    {
        std::string text;
        if (!hooks_.read_reply_text || !hooks_.read_reply_text(text)) return;
        bool valid_text = false;
        if (!text.empty() && text.size() <= hooks_.max_text_bytes)
        {
            for (unsigned char character : text)
            {
                if (!std::isspace(character))
                {
                    valid_text = true;
                    break;
                }
            }
        }
        const auto conversation = conversationIdForMessage(message);
        const NodeId self = hooks_.self_node ? hooks_.self_node() : 0;
        const Context context{true, valid_text,
                              hooks_.screen_sleeping && hooks_.screen_sleeping(),
                              self != 0 && message.from == self,
                              service_.canSendToConversation(conversation)};
        if (decide(message, context) == Decision::Reply)
        {
            (void)service_.sendTextToConversationDetailed(conversation, text);
        }
    }

  private:
    ChatService& service_;
    ObserverHooks hooks_;
};
} // namespace chat::auto_reply
