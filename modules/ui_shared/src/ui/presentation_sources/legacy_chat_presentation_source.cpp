#include "ui/presentation_sources/legacy_chat_presentation_source.h"

#include "chat_presentation_adapters/chat_conversation_mapper.h"
#include "chat_presentation_adapters/chat_message_mapper.h"
#include "ui_presentation/common/fixed_text.h"

#include <cstddef>
#include <cstdio>

namespace ui::presentation_sources
{
namespace
{

constexpr std::size_t kMaxConversationRows = 16;
constexpr std::size_t kMaxMessageRows = 24;

template <std::size_t N>
void copyString(ui::FixedText<N>& out, const std::string& text)
{
    ui::copyText(out, text.c_str());
}

void copyNodeLabel(ui::FixedText<32>& out, ::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        ui::copyText(out, "Me");
        return;
    }

    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04lX",
                  static_cast<unsigned long>(node_id & 0xFFFFU));
    ui::copyText(out, buffer);
}

void copyTimeLabel(ui::FixedText<24>& out, uint32_t timestamp)
{
    if (timestamp == 0)
    {
        out.clear();
        return;
    }

    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%lu",
                  static_cast<unsigned long>(timestamp));
    ui::copyText(out, buffer);
}

} // namespace

LegacyChatPresentationSource::LegacyChatPresentationSource(
    ::chat::ChatService& chat_service,
    ::chat::contacts::ContactService* contact_service)
    : chat_service_(chat_service),
      contact_service_(contact_service)
{
}

bool LegacyChatPresentationSource::buildChatWorkspaceSnapshot(
    const ui::chat::ChatWorkspaceRequest& request,
    ui::chat::ChatWorkspaceSnapshot& out) const
{
    out = ui::chat::ChatWorkspaceSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    ui::copyText(out.workspace_title, "Chat");
    ui::copyText(out.composer_placeholder, "Message");

    std::size_t total = 0;
    const auto conversations = chat_service_.getConversations(
        request.conversation_offset,
        kMaxConversationRows,
        &total);

    out.conversation_count = conversations.size() < kMaxConversationRows
                                 ? conversations.size()
                                 : kMaxConversationRows;
    for (std::size_t i = 0; i < out.conversation_count; ++i)
    {
        const ::chat::ConversationMeta& meta = conversations[i];
        ui::chat::ConversationRow& row = out.conversations[i];
        row.id = chat_presentation_adapters::toUiConversationId(meta.id);
        row.kind = row.id.kind;
        row.protocol = row.id.protocol;
        row.unread_count = meta.unread < 0 ? 0U : static_cast<uint16_t>(meta.unread);
        row.selected = row.id == request.selected;
        copyString(row.title, meta.name);
        copyString(row.subtitle, meta.preview);
    }

    out.selected_conversation = request.selected;

    ::chat::ConversationId core_selected;
    if (chat_presentation_adapters::toCoreConversationId(request.selected,
                                                        core_selected))
    {
        const auto messages =
            chat_service_.getRecentMessages(core_selected, kMaxMessageRows);
        out.message_count = messages.size() < kMaxMessageRows
                                ? messages.size()
                                : kMaxMessageRows;
        for (std::size_t i = 0; i < out.message_count; ++i)
        {
            const ::chat::ChatMessage& message = messages[i];
            ui::chat::MessageRow& row = out.messages[i];
            const ::chat::ConversationId core_conversation(
                message.channel,
                message.peer,
                message.protocol);
            row.conversation =
                chat_presentation_adapters::toUiConversationId(core_conversation);
            row.ref = chat_presentation_adapters::toUiMessageRef(message);
            row.delivery =
                chat_presentation_adapters::mapMessageStatus(message.status);
            row.failure =
                chat_presentation_adapters::mapMessageFailure(message.status);
            row.outgoing = message.status != ::chat::MessageStatus::Incoming;
            copyString(row.text, message.text);
            copyTimeLabel(row.time_label, message.timestamp);

            if (row.outgoing)
            {
                ui::copyText(row.sender_label, "Me");
            }
            else if (contact_service_ != nullptr)
            {
                const std::string contact =
                    contact_service_->getContactName(message.from);
                if (!contact.empty())
                {
                    ui::copyText(row.sender_label, contact.c_str());
                }
                else
                {
                    copyNodeLabel(row.sender_label, message.from);
                }
            }
            else
            {
                copyNodeLabel(row.sender_label, message.from);
            }
        }
    }

    const bool selected_supported =
        request.selected.kind == ui::chat::ConversationKind::DirectPeer ||
        request.selected.kind == ui::chat::ConversationKind::Channel;
    out.can_send = request.selected.isValid() && selected_supported;
    out.composer_enabled = out.can_send;
    return true;
}

} // namespace ui::presentation_sources
