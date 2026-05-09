#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "chat/domain/chat_types.h"

namespace trailmate::linux_app
{
class LinuxAppServices;
}

namespace trailmate::uconsole
{

struct ChatConversationItem
{
    ::chat::ConversationId id{};
    std::string title{};
    std::string preview{};
    std::string meta{};
    int unread = 0;
    bool active = false;
};

struct ChatMessageItem
{
    std::string sender{};
    std::string text{};
    std::string meta{};
    bool outgoing = false;
    bool failed = false;
};

struct ChatWorkspaceSnapshot
{
    std::vector<ChatConversationItem> conversations{};
    std::vector<ChatMessageItem> messages{};
    ::chat::ConversationId active_conversation{};
    std::string active_title{};
    std::string active_meta{};
    std::string action_status{};
    std::size_t total_conversations = 0;
    int total_unread = 0;
    bool can_send = true;
};

class UConsoleChatWorkspaceModel final
{
  public:
    explicit UConsoleChatWorkspaceModel(linux_app::LinuxAppServices& services);

    [[nodiscard]] ChatWorkspaceSnapshot snapshot(std::size_t conversation_limit,
                                                 std::size_t message_limit);

    bool selectConversationAt(std::size_t index,
                              std::size_t conversation_limit);
    bool selectConversation(const ::chat::ConversationId& conversation);
    bool selectPrimaryConversation();
    bool sendText(const std::string& text);

    [[nodiscard]] const ::chat::ConversationId& activeConversation() const
    {
        return active_conversation_;
    }

  private:
    void ensureActiveConversation();
    [[nodiscard]] ::chat::ConversationId primaryConversation() const;
    [[nodiscard]] bool canSendActiveConversation() const;
    [[nodiscard]] std::vector<::chat::ConversationMeta> loadConversationPage(
        std::size_t limit, std::size_t* total) const;

    linux_app::LinuxAppServices& services_;
    ::chat::ConversationId active_conversation_{};
    std::vector<::chat::ConversationId> displayed_conversations_{};
    std::string action_status_{};
    bool active_initialized_ = false;
};

} // namespace trailmate::uconsole
