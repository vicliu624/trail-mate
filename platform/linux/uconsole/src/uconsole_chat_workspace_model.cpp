#include "uconsole/uconsole_chat_workspace_model.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/linux_app_services.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "sys/clock.h"

namespace trailmate::uconsole
{
namespace
{

[[nodiscard]] const char* protocolLabel(::chat::MeshProtocol protocol) noexcept
{
    switch (protocol)
    {
    case ::chat::MeshProtocol::Meshtastic:
        return "Meshtastic";
    case ::chat::MeshProtocol::MeshCore:
        return "MeshCore";
    case ::chat::MeshProtocol::RNode:
        return "RNode";
    case ::chat::MeshProtocol::LXMF:
        return "LXMF";
    }
    return "Unknown";
}

[[nodiscard]] const char* statusLabel(::chat::MessageStatus status) noexcept
{
    switch (status)
    {
    case ::chat::MessageStatus::Incoming:
        return "received";
    case ::chat::MessageStatus::Queued:
        return "queued";
    case ::chat::MessageStatus::Sent:
        return "sent";
    case ::chat::MessageStatus::Failed:
        return "failed";
    }
    return "unknown";
}

[[nodiscard]] std::string formatNodeId(std::uint32_t node_id)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%08lX",
                  static_cast<unsigned long>(node_id));
    return buffer;
}

[[nodiscard]] std::string formatChannel(::chat::ChannelId channel)
{
    switch (channel)
    {
    case ::chat::ChannelId::PRIMARY:
        return "Primary";
    case ::chat::ChannelId::SECONDARY:
        return "Secondary";
    case ::chat::ChannelId::MAX_CHANNELS:
        return "Channel";
    }
    return "Channel";
}

[[nodiscard]] std::string formatAge(std::uint32_t timestamp)
{
    if (timestamp == 0) return "no activity";

    const std::uint32_t now = sys::epoch_seconds_now();
    if (timestamp >= now) return "now";

    const std::uint32_t age = now - timestamp;
    if (age < 60U) return "now";
    if (age < 3600U)
    {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%lum ago",
                      static_cast<unsigned long>(age / 60U));
        return buffer;
    }
    if (age < 86400U)
    {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%luh ago",
                      static_cast<unsigned long>(age / 3600U));
        return buffer;
    }

    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lud ago",
                  static_cast<unsigned long>(age / 86400U));
    return buffer;
}

[[nodiscard]] std::string trimCopy(const std::string& text)
{
    const auto is_space = [](unsigned char ch)
    {
        return std::isspace(ch) != 0;
    };

    auto begin = std::find_if_not(text.begin(), text.end(), is_space);
    auto end = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
    if (begin >= end) return {};
    return std::string(begin, end);
}

[[nodiscard]] bool sameConversation(const ::chat::ConversationId& left,
                                    const ::chat::ConversationId& right) noexcept
{
    return left == right;
}

[[nodiscard]] std::string titleForConversation(
    const ::chat::ConversationId& id, const std::string& stored_name)
{
    if (!stored_name.empty() && stored_name != "Broadcast") return stored_name;
    if (id.peer == 0)
    {
        return formatChannel(id.channel) + " broadcast";
    }
    return "Direct " + formatNodeId(id.peer);
}

[[nodiscard]] std::string metaForConversation(
    const ::chat::ConversationMeta& conversation)
{
    std::string meta = protocolLabel(conversation.id.protocol);
    meta += " / ";
    meta += formatChannel(conversation.id.channel);
    if (conversation.id.peer != 0)
    {
        meta += " / ";
        meta += formatNodeId(conversation.id.peer);
    }
    meta += " / ";
    meta += formatAge(conversation.last_timestamp);
    if (conversation.unread > 0)
    {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), " / %d unread",
                      conversation.unread);
        meta += buffer;
    }
    return meta;
}

[[nodiscard]] ChatConversationItem makeConversationItem(
    const ::chat::ConversationMeta& conversation,
    const ::chat::ConversationId& active)
{
    ChatConversationItem item{};
    item.id = conversation.id;
    item.title = titleForConversation(conversation.id, conversation.name);
    item.preview = conversation.preview.empty()
                       ? "No messages in this thread yet."
                       : conversation.preview;
    item.meta = metaForConversation(conversation);
    item.unread = conversation.unread;
    item.active = sameConversation(conversation.id, active);
    return item;
}

[[nodiscard]] ChatMessageItem makeMessageItem(
    const ::chat::ChatMessage& message, ::chat::NodeId self_node)
{
    ChatMessageItem item{};
    item.outgoing = message.from == 0 || message.from == self_node;
    item.failed = message.status == ::chat::MessageStatus::Failed;
    item.sender = item.outgoing ? "You" : formatNodeId(message.from);
    item.text = message.text.empty() ? "(empty)" : message.text;
    item.meta = statusLabel(message.status);
    item.meta += " / ";
    item.meta += formatAge(message.timestamp);
    if (message.msg_id != 0)
    {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), " / #%08lX",
                      static_cast<unsigned long>(message.msg_id));
        item.meta += buffer;
    }
    return item;
}

[[nodiscard]] ::chat::ConversationMeta makeSyntheticPrimary(
    const ::chat::ConversationId& id)
{
    ::chat::ConversationMeta conversation{};
    conversation.id = id;
    conversation.name = "Broadcast";
    conversation.preview = "No messages in this thread yet.";
    conversation.last_timestamp = 0;
    conversation.unread = 0;
    return conversation;
}

} // namespace

UConsoleChatWorkspaceModel::UConsoleChatWorkspaceModel(
    linux_app::LinuxAppServices& services)
    : services_(services)
{
}

ChatWorkspaceSnapshot UConsoleChatWorkspaceModel::snapshot(
    std::size_t conversation_limit, std::size_t message_limit)
{
    ensureActiveConversation();

    ChatWorkspaceSnapshot out{};
    out.active_conversation = active_conversation_;
    out.action_status = action_status_;
    out.total_unread = services_.chat().getTotalUnread();
    out.can_send = canSendActiveConversation();

    std::size_t total = 0;
    auto conversations = loadConversationPage(conversation_limit, &total);
    out.total_conversations = total;

    if (!conversations.empty())
    {
        const bool active_visible =
            std::any_of(conversations.begin(), conversations.end(),
                        [this](const auto& conversation)
                        {
                            return sameConversation(conversation.id,
                                                    active_conversation_);
                        });
        if (!active_visible)
        {
            auto active_meta = makeSyntheticPrimary(active_conversation_);
            active_meta.name = titleForConversation(active_conversation_, {});
            conversations.insert(conversations.begin(), std::move(active_meta));
        }
    }

    out.conversations.reserve(conversations.size());
    displayed_conversations_.clear();
    displayed_conversations_.reserve(conversations.size());
    for (const auto& conversation : conversations)
    {
        out.conversations.push_back(
            makeConversationItem(conversation, active_conversation_));
        displayed_conversations_.push_back(conversation.id);
    }

    auto active_it =
        std::find_if(conversations.begin(), conversations.end(),
                     [this](const auto& conversation)
                     {
                         return sameConversation(conversation.id,
                                                 active_conversation_);
                     });
    if (active_it != conversations.end())
    {
        out.active_title =
            titleForConversation(active_it->id, active_it->name);
        out.active_meta = metaForConversation(*active_it);
    }
    else
    {
        out.active_title = titleForConversation(active_conversation_, {});
        out.active_meta = protocolLabel(active_conversation_.protocol);
    }

    auto messages =
        services_.chat().getRecentMessages(active_conversation_, message_limit);
    out.messages.reserve(messages.size());
    const ::chat::NodeId self_node = services_.selfNodeId();
    for (const auto& message : messages)
    {
        out.messages.push_back(makeMessageItem(message, self_node));
    }

    return out;
}

bool UConsoleChatWorkspaceModel::selectConversationAt(
    std::size_t index, std::size_t conversation_limit)
{
    if (displayed_conversations_.empty())
    {
        static_cast<void>(snapshot(conversation_limit, 0));
    }
    if (index < displayed_conversations_.size())
    {
        return selectConversation(displayed_conversations_[index]);
    }

    std::size_t total = 0;
    auto conversations = loadConversationPage(conversation_limit, &total);
    if (conversations.empty() && index == 0)
    {
        return selectPrimaryConversation();
    }
    if (index >= conversations.size())
    {
        return false;
    }
    return selectConversation(conversations[index].id);
}

bool UConsoleChatWorkspaceModel::selectConversation(
    const ::chat::ConversationId& conversation)
{
    active_conversation_ = conversation;
    active_initialized_ = true;
    services_.chat().markConversationRead(active_conversation_);
    action_status_ = "Conversation selected.";
    return true;
}

bool UConsoleChatWorkspaceModel::selectPrimaryConversation()
{
    return selectConversation(primaryConversation());
}

bool UConsoleChatWorkspaceModel::sendText(const std::string& text)
{
    ensureActiveConversation();
    const std::string trimmed = trimCopy(text);
    if (trimmed.empty())
    {
        action_status_ = "Type a message before sending.";
        return false;
    }
    if (!canSendActiveConversation())
    {
        action_status_ = "No Linux mesh transport is connected.";
        return false;
    }

    const ::chat::MessageId message_id =
        services_.chat().sendText(active_conversation_.channel, trimmed,
                                  active_conversation_.peer);
    if (message_id == 0)
    {
        action_status_ = "Message failed to queue.";
        return false;
    }

    action_status_ = "Message queued.";
    return true;
}

void UConsoleChatWorkspaceModel::ensureActiveConversation()
{
    if (active_initialized_) return;
    active_conversation_ = primaryConversation();
    active_initialized_ = true;
}

::chat::ConversationId UConsoleChatWorkspaceModel::primaryConversation() const
{
    return ::chat::ConversationId(::chat::ChannelId::PRIMARY, 0,
                                  services_.meshProtocol());
}

bool UConsoleChatWorkspaceModel::canSendActiveConversation() const
{
    const auto* adapter = services_.meshAdapter();
    if (adapter == nullptr || !adapter->isReady())
    {
        return false;
    }

    const ::chat::MeshCapabilities capabilities = adapter->getCapabilities();
    return capabilities.supports_unicast_text;
}

std::vector<::chat::ConversationMeta>
UConsoleChatWorkspaceModel::loadConversationPage(std::size_t limit,
                                                 std::size_t* total) const
{
    return services_.chat().getConversations(0, limit, total);
}

} // namespace trailmate::uconsole
