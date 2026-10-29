#pragma once

#include <mutex>
#include <vector>

#include "chat/ports/i_chat_store.h"

namespace trailmate::linux_app
{

class LinuxSqliteChatStore final : public ::chat::IChatStore
{
  public:
    LinuxSqliteChatStore();
    ~LinuxSqliteChatStore() override;

    void append(const ::chat::ChatMessage& msg) override;
    bool appendDurably(const ::chat::ChatMessage& msg) override;
    bool appendIncomingDurably(const ::chat::ChatMessage& msg) override;
    bool hasReticulumLxmfMessageHash(const uint8_t* hash) const override;
    std::vector<::chat::ChatMessage> loadRecent(
        const ::chat::ConversationId& conv,
        std::size_t n) override;
    std::vector<::chat::ChatMessage> loadPageFromLatest(
        const ::chat::ConversationId& conv,
        std::size_t offset_from_latest,
        std::size_t limit,
        std::size_t* total) override;
    std::vector<::chat::ConversationMeta> loadConversationPage(
        std::size_t offset,
        std::size_t limit,
        std::size_t* total) override;
    bool setUnread(const ::chat::ConversationId& conv, int unread) override;
    int getUnread(const ::chat::ConversationId& conv) const override;
    void clearConversation(const ::chat::ConversationId& conv) override;
    void clearAll() override;
    bool updateMessageStatus(::chat::MessageId msg_id,
                             ::chat::MessageStatus status) override;
    bool updateMessageStatusForProtocol(::chat::MessageId msg_id,
                                        ::chat::MeshProtocol protocol,
                                        ::chat::MessageStatus status) override;
    bool getMessage(::chat::MessageId msg_id,
                    ::chat::ChatMessage* out) const override;
    bool getMessageForProtocol(::chat::MessageId msg_id,
                               ::chat::MeshProtocol protocol,
                               ::chat::ChatMessage* out) const override;
    void flush() override;

  private:
    mutable std::mutex mutex_;
};

} // namespace trailmate::linux_app
