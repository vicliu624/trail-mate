#pragma once

#include "ui_presentation/chat/chat_identity.h"
#include "ui_presentation/chat/chat_message_ref.h"
#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stddef.h>
#include <stdint.h>

namespace ui::chat
{

struct ConversationRow
{
    ConversationId id;

    ui::FixedText<32> title;
    ui::FixedText<64> subtitle;

    uint16_t unread_count = 0;
    bool selected = false;
    bool muted = false;

    ConversationKind kind = ConversationKind::None;
    ChatProtocolKind protocol = ChatProtocolKind::None;

    MessageDeliveryState last_delivery = MessageDeliveryState::Unknown;
};

struct MessageRow
{
    MessageRef ref;
    ConversationId conversation;

    bool outgoing = false;
    MessageDeliveryState delivery = MessageDeliveryState::Unknown;
    MessageFailureKind failure = MessageFailureKind::None;

    ui::FixedText<160> text;
    ui::FixedText<24> time_label;
    ui::FixedText<32> sender_label;
};

struct ChatWorkspaceSnapshot
{
    ui::SnapshotHeader header;

    ConversationRow conversations[16]{};
    size_t conversation_count = 0;

    MessageRow messages[24]{};
    size_t message_count = 0;

    ConversationId selected_conversation;

    bool can_send = false;
    bool composer_enabled = false;

    ui::FixedText<64> composer_placeholder;
    ui::FixedText<64> workspace_title;
};

struct SendMessageView
{
    ConversationId conversation;
    const char* text = nullptr;
    size_t text_len = 0;
};

} // namespace ui::chat
