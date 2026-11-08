#include "chat/delivery/chat_message_ledger.h"
#include "chat/domain/conversation_name.h"

#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/delivery/chat_delivery_send_result_projection.h"
#include "chat/delivery/chat_outbox_service.h"

#include <algorithm>
#include <cstdio>

namespace chat::delivery
{

ChatMessageLedger::ChatMessageLedger(ChatModel& model, IChatStore& store)
    : model_(model), store_(store)
{
}

void ChatMessageLedger::setDeliveryEventPort(
    IChatDeliveryEventPort* delivery_event_port)
{
    delivery_event_port_ = delivery_event_port;
}

LedgerPersistence ChatMessageLedger::recordOutbound(
    const ChatMessage& message,
    bool model_enabled,
    SendFailureKind failure)
{
    trackOutbound(message);
    if (model_enabled)
    {
        model_.onSendQueued(message);
        if (message.status == MessageStatus::Failed && message.msg_id != 0)
        {
            model_.updateMessageStatusForProtocol(message.msg_id,
                                                  message.protocol,
                                                  MessageStatus::Failed);
        }
    }
    LedgerPersistence persistence = LedgerPersistence::Durable;
    if (!store_.appendDurably(message))
    {
        persistence = enqueuePendingOutbound(message)
                          ? LedgerPersistence::Deferred
                          : LedgerPersistence::Rejected;
    }
    if (ChatOutboxService::isOutboundStatusUpdate(message.status))
    {
        publishDeliveryEvent(message, message.status, 0, failure);
    }
    return persistence;
}

LedgerPersistence ChatMessageLedger::recordIncoming(
    const ChatMessage& message)
{
    return store_.appendIncomingDurably(message)
               ? LedgerPersistence::Durable
               : LedgerPersistence::Deferred;
}

bool ChatMessageLedger::applyOutboundStatus(MessageId msg_id,
                                            MessageStatus status,
                                            bool model_enabled,
                                            uint32_t timestamp_ms,
                                            SendFailureKind failure)
{
    if (TrackedOutbound* tracked = findTrackedOutbound(msg_id))
    {
        ChatMessage current{};
        current.msg_id = msg_id;
        current.protocol = tracked->protocol;
        current.status = tracked->status;
        if (!ChatOutboxService::shouldApplyStatus(&current, status) ||
            !writeStatusForProtocol(
                msg_id, tracked->protocol, status, model_enabled))
        {
            return false;
        }
        publishDeliveryEventForProtocol(
            msg_id, tracked->protocol, status, timestamp_ms, failure);
        return true;
    }

    ChatMessage current{};
    if (!lookupMessage(msg_id, current))
    {
        return false;
    }
    if (!ChatOutboxService::shouldApplyStatus(&current, status))
    {
        return false;
    }
    if (!writeStatusForProtocol(msg_id,
                                current.protocol,
                                status,
                                model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, status, timestamp_ms, failure);
    return true;
}

bool ChatMessageLedger::applyOutboundStatusForProtocol(MessageId msg_id,
                                                       MeshProtocol protocol,
                                                       MessageStatus status,
                                                       bool model_enabled,
                                                       uint32_t timestamp_ms,
                                                       SendFailureKind failure)
{
    if (TrackedOutbound* tracked =
            findTrackedOutboundForProtocol(msg_id, protocol))
    {
        ChatMessage current{};
        current.msg_id = msg_id;
        current.protocol = protocol;
        current.status = tracked->status;
        if (!ChatOutboxService::shouldApplyStatus(&current, status) ||
            !writeStatusForProtocol(msg_id, protocol, status, model_enabled))
        {
            return false;
        }
        publishDeliveryEventForProtocol(
            msg_id, protocol, status, timestamp_ms, failure);
        return true;
    }

    ChatMessage current{};
    if (!lookupMessageForProtocol(msg_id, protocol, current))
    {
        return false;
    }
    if (!ChatOutboxService::shouldApplyStatus(&current, status))
    {
        return false;
    }
    if (!writeStatusForProtocol(msg_id, protocol, status, model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, status, timestamp_ms, failure);
    return true;
}

bool ChatMessageLedger::markRetryQueued(MessageId msg_id, bool model_enabled)
{
    ChatMessage current{};
    if (!lookupMessage(msg_id, current))
    {
        return false;
    }
    if (current.from != 0 || current.status != MessageStatus::Failed)
    {
        return false;
    }
    if (!writeStatusForProtocol(msg_id,
                                current.protocol,
                                MessageStatus::Queued,
                                model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, MessageStatus::Queued);
    return true;
}

bool ChatMessageLedger::markRetryQueuedForProtocol(MessageId msg_id,
                                                   MeshProtocol protocol,
                                                   bool model_enabled)
{
    ChatMessage current{};
    if (!lookupMessageForProtocol(msg_id, protocol, current))
    {
        return false;
    }
    if (current.from != 0 || current.status != MessageStatus::Failed)
    {
        return false;
    }
    if (!writeStatusForProtocol(msg_id,
                                protocol,
                                MessageStatus::Queued,
                                model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, MessageStatus::Queued);
    return true;
}

bool ChatMessageLedger::lookupMessage(MessageId msg_id, ChatMessage& out) const
{
    if (msg_id == 0)
    {
        return false;
    }
    if (const PendingOutboundWrite* pending = findPendingOutbound(msg_id))
    {
        out = pending->message;
        applyPendingStatus(out);
        return true;
    }
    if (const ChatMessage* message = model_.getMessage(msg_id))
    {
        out = *message;
        applyPendingStatus(out);
        return true;
    }
    if (!store_.getMessage(msg_id, &out))
    {
        return false;
    }
    applyPendingStatus(out);
    return true;
}

bool ChatMessageLedger::lookupMessageForProtocol(MessageId msg_id,
                                                 MeshProtocol protocol,
                                                 ChatMessage& out) const
{
    if (msg_id == 0)
    {
        return false;
    }
    if (const PendingOutboundWrite* pending =
            findPendingOutboundForProtocol(msg_id, protocol))
    {
        out = pending->message;
        applyPendingStatus(out);
        return true;
    }
    if (const ChatMessage* message =
            model_.getMessageForProtocol(msg_id, protocol))
    {
        out = *message;
        applyPendingStatus(out);
        return true;
    }
    if (!store_.getMessageForProtocol(msg_id, protocol, &out))
    {
        return false;
    }
    applyPendingStatus(out);
    return true;
}

bool ChatMessageLedger::writeStatusForProtocol(MessageId msg_id,
                                               MeshProtocol protocol,
                                               MessageStatus status,
                                               bool model_enabled)
{
    if (msg_id == 0)
    {
        return false;
    }
    bool updated = false;
    if (model_enabled)
    {
        updated =
            model_.updateMessageStatusForProtocol(msg_id, protocol, status);
    }
    if (PendingOutboundWrite* pending =
            findPendingOutboundForProtocol(msg_id, protocol))
    {
        pending->message.status = status;
        return true;
    }
    if (store_.updateMessageStatusForProtocol(msg_id, protocol, status))
    {
        if (TrackedOutbound* tracked =
                findTrackedOutboundForProtocol(msg_id, protocol))
        {
            tracked->status = status;
        }
        removePendingStatus(msg_id, protocol);
        return true;
    }
    const bool deferred = enqueuePendingStatus(msg_id, protocol, status);
    if (deferred)
    {
        if (TrackedOutbound* tracked =
                findTrackedOutboundForProtocol(msg_id, protocol))
        {
            tracked->status = status;
        }
    }
    return deferred || updated;
}

std::size_t ChatMessageLedger::flushPendingWrites(std::size_t budget)
{
    std::size_t flushed = 0;
    while (flushed < budget)
    {
        const int outbound_index = oldestPendingOutboundIndex();
        if (outbound_index >= 0)
        {
            PendingOutboundWrite& pending =
                pending_outbound_writes_[static_cast<std::size_t>(outbound_index)];
            if (!store_.appendDurably(pending.message))
            {
                break;
            }
            removePendingStatus(pending.message.msg_id,
                                pending.message.protocol);
            pending = PendingOutboundWrite{};
            ++flushed;
            continue;
        }

        const int status_index = oldestPendingStatusIndex();
        if (status_index < 0)
        {
            break;
        }
        PendingStatusWrite& pending =
            pending_status_writes_[static_cast<std::size_t>(status_index)];
        if (!store_.updateMessageStatusForProtocol(pending.msg_id,
                                                   pending.protocol,
                                                   pending.status))
        {
            break;
        }
        pending = PendingStatusWrite{};
        ++flushed;
    }
    return flushed;
}

std::vector<ChatMessage> ChatMessageLedger::loadPageFromLatest(
    const ConversationId& conversation,
    std::size_t offset_from_latest,
    std::size_t limit,
    std::size_t* total) const
{
    std::array<const PendingOutboundWrite*, kPendingOutboundWriteDepth>
        pending{};
    std::size_t pending_count = 0;
    for (const PendingOutboundWrite& entry : pending_outbound_writes_)
    {
        if (entry.used &&
            conversationIdForMessage(entry.message) == conversation)
        {
            pending[pending_count++] = &entry;
        }
    }
    std::sort(pending.begin(),
              pending.begin() + static_cast<long>(pending_count),
              [](const PendingOutboundWrite* lhs,
                 const PendingOutboundWrite* rhs)
              {
                  return lhs->sequence < rhs->sequence;
              });

    std::size_t durable_total = 0;
    std::vector<ChatMessage> result;
    if (limit == 0)
    {
        (void)store_.loadPageFromLatest(conversation, 0, 1, &durable_total);
        if (total)
        {
            *total = durable_total + pending_count;
        }
        return result;
    }

    if (offset_from_latest >= pending_count)
    {
        result = store_.loadPageFromLatest(conversation,
                                           offset_from_latest - pending_count,
                                           limit,
                                           &durable_total);
    }
    else
    {
        const std::size_t pending_end = pending_count - offset_from_latest;
        const std::size_t pending_start =
            pending_end > limit ? pending_end - limit : 0;
        const std::size_t pending_take = pending_end - pending_start;
        const std::size_t durable_take = limit - pending_take;
        if (durable_take > 0)
        {
            result = store_.loadPageFromLatest(conversation,
                                               0,
                                               durable_take,
                                               &durable_total);
        }
        else
        {
            (void)store_.loadPageFromLatest(conversation,
                                            0,
                                            1,
                                            &durable_total);
        }
        result.reserve(result.size() + pending_take);
        for (std::size_t index = pending_start; index < pending_end; ++index)
        {
            result.push_back(pending[index]->message);
        }
    }

    for (ChatMessage& message : result)
    {
        applyPendingStatus(message);
    }
    if (total)
    {
        *total = durable_total + pending_count;
    }
    return result;
}

std::vector<ChatMessage> ChatMessageLedger::loadRecent(
    const ConversationId& conversation,
    std::size_t limit) const
{
    return loadPageFromLatest(conversation, 0, limit, nullptr);
}

std::vector<ConversationMeta> ChatMessageLedger::loadConversationPage(
    MeshProtocol protocol,
    std::size_t offset,
    std::size_t limit,
    std::size_t* total) const
{
    protocol = protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                               : protocol;
    std::size_t durable_total = 0;
    std::vector<ConversationMeta> conversations =
        store_.loadConversationPageForProtocol(protocol,
                                               0,
                                               0,
                                               &durable_total);

    std::array<const PendingOutboundWrite*, kPendingOutboundWriteDepth>
        pending{};
    std::size_t pending_count = 0;
    for (const PendingOutboundWrite& entry : pending_outbound_writes_)
    {
        const MeshProtocol entry_protocol =
            entry.message.protocol == MeshProtocol::RNode
                ? MeshProtocol::Reticulum
                : entry.message.protocol;
        if (entry.used && entry_protocol == protocol)
        {
            pending[pending_count++] = &entry;
        }
    }
    std::sort(pending.begin(),
              pending.begin() + static_cast<long>(pending_count),
              [](const PendingOutboundWrite* lhs,
                 const PendingOutboundWrite* rhs)
              {
                  return lhs->sequence < rhs->sequence;
              });

    for (std::size_t pending_index = 0;
         pending_index < pending_count;
         ++pending_index)
    {
        const ChatMessage& message = pending[pending_index]->message;
        const ConversationId conversation = conversationIdForMessage(message);
        auto existing = std::find_if(
            conversations.begin(),
            conversations.end(),
            [&](const ConversationMeta& meta)
            {
                return meta.id == conversation;
            });
        if (existing == conversations.end())
        {
            ConversationMeta meta{};
            meta.id = conversation;
            meta.name = fallbackConversationName(conversation);
            conversations.push_back(meta);
            existing = conversations.end() - 1;
        }
        existing->preview = message.text;
        existing->last_timestamp = message.timestamp;
        existing->reticulum_identity = message.reticulum_identity;
    }

    std::stable_sort(conversations.begin(),
                     conversations.end(),
                     [](const ConversationMeta& lhs,
                        const ConversationMeta& rhs)
                     {
                         return lhs.last_timestamp > rhs.last_timestamp;
                     });
    if (total)
    {
        *total = conversations.size();
    }
    if (offset >= conversations.size())
    {
        return {};
    }
    const std::size_t end =
        limit == 0
            ? conversations.size()
            : std::min(conversations.size(), offset + limit);
    return std::vector<ConversationMeta>(
        conversations.begin() + static_cast<long>(offset),
        conversations.begin() + static_cast<long>(end));
}

bool ChatMessageLedger::findMessage(MessageId msg_id, ChatMessage& out) const
{
    return lookupMessage(msg_id, out);
}

bool ChatMessageLedger::findMessageForProtocol(MessageId msg_id,
                                               MeshProtocol protocol,
                                               ChatMessage& out) const
{
    return lookupMessageForProtocol(msg_id, protocol, out);
}

void ChatMessageLedger::clearConversation(
    const ConversationId& conversation)
{
    for (PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        if (!pending.used ||
            !(conversationIdForMessage(pending.message) == conversation))
        {
            continue;
        }
        removePendingStatus(pending.message.msg_id,
                            pending.message.protocol);
        pending = PendingOutboundWrite{};
    }

    for (PendingStatusWrite& pending : pending_status_writes_)
    {
        if (!pending.used)
        {
            continue;
        }
        ChatMessage message{};
        if (lookupMessageForProtocol(pending.msg_id,
                                     pending.protocol,
                                     message) &&
            conversationIdForMessage(message) == conversation)
        {
            pending = PendingStatusWrite{};
        }
    }
    clearTrackedOutbound(conversation);
}

void ChatMessageLedger::clear()
{
    for (PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        pending = PendingOutboundWrite{};
    }
    for (PendingStatusWrite& pending : pending_status_writes_)
    {
        pending = PendingStatusWrite{};
    }
    for (TrackedOutbound& tracked : tracked_outbound_)
    {
        tracked = TrackedOutbound{};
    }
    next_pending_sequence_ = 1;
}

ChatMessageLedger::PendingOutboundWrite*
ChatMessageLedger::findPendingOutbound(MessageId msg_id)
{
    if (msg_id == 0)
    {
        return nullptr;
    }
    for (PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        if (pending.used && pending.message.msg_id == msg_id)
        {
            return &pending;
        }
    }
    return nullptr;
}

ChatMessageLedger::PendingOutboundWrite*
ChatMessageLedger::findPendingOutboundForProtocol(MessageId msg_id,
                                                  MeshProtocol protocol)
{
    if (msg_id == 0)
    {
        return nullptr;
    }
    for (PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        if (pending.used && pending.message.msg_id == msg_id &&
            pending.message.protocol == protocol)
        {
            return &pending;
        }
    }
    return nullptr;
}

const ChatMessageLedger::PendingOutboundWrite*
ChatMessageLedger::findPendingOutbound(MessageId msg_id) const
{
    if (msg_id == 0)
    {
        return nullptr;
    }
    for (const PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        if (pending.used && pending.message.msg_id == msg_id)
        {
            return &pending;
        }
    }
    return nullptr;
}

const ChatMessageLedger::PendingOutboundWrite*
ChatMessageLedger::findPendingOutboundForProtocol(
    MessageId msg_id,
    MeshProtocol protocol) const
{
    if (msg_id == 0)
    {
        return nullptr;
    }
    for (const PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        if (pending.used && pending.message.msg_id == msg_id &&
            pending.message.protocol == protocol)
        {
            return &pending;
        }
    }
    return nullptr;
}

bool ChatMessageLedger::enqueuePendingOutbound(const ChatMessage& message)
{
    if (PendingOutboundWrite* existing =
            findPendingOutboundForProtocol(message.msg_id,
                                           message.protocol))
    {
        existing->message = message;
        return true;
    }
    for (PendingOutboundWrite& pending : pending_outbound_writes_)
    {
        if (pending.used)
        {
            continue;
        }
        pending.used = true;
        pending.sequence = nextPendingSequence();
        pending.message = message;
        return true;
    }
    return false;
}

bool ChatMessageLedger::enqueuePendingStatus(MessageId msg_id,
                                             MeshProtocol protocol,
                                             MessageStatus status)
{
    for (PendingStatusWrite& pending : pending_status_writes_)
    {
        if (pending.used && pending.msg_id == msg_id &&
            pending.protocol == protocol)
        {
            pending.status = status;
            return true;
        }
    }
    for (PendingStatusWrite& pending : pending_status_writes_)
    {
        if (pending.used)
        {
            continue;
        }
        pending.used = true;
        pending.sequence = nextPendingSequence();
        pending.msg_id = msg_id;
        pending.protocol = protocol;
        pending.status = status;
        return true;
    }
    return false;
}

void ChatMessageLedger::trackOutbound(const ChatMessage& message)
{
    if (message.msg_id == 0 || message.from != 0)
    {
        return;
    }
    if (TrackedOutbound* existing =
            findTrackedOutboundForProtocol(message.msg_id, message.protocol))
    {
        existing->conversation = conversationIdForMessage(message);
        existing->status = message.status;
        return;
    }

    TrackedOutbound* target = nullptr;
    for (TrackedOutbound& tracked : tracked_outbound_)
    {
        if (!tracked.used)
        {
            target = &tracked;
            break;
        }
        if (target == nullptr || tracked.sequence < target->sequence)
        {
            target = &tracked;
        }
    }
    if (target == nullptr)
    {
        return;
    }
    target->used = true;
    target->sequence = nextPendingSequence();
    target->conversation = conversationIdForMessage(message);
    target->msg_id = message.msg_id;
    target->protocol = message.protocol;
    target->status = message.status;
}

ChatMessageLedger::TrackedOutbound*
ChatMessageLedger::findTrackedOutbound(MessageId msg_id)
{
    if (msg_id == 0)
    {
        return nullptr;
    }
    TrackedOutbound* match = nullptr;
    for (TrackedOutbound& tracked : tracked_outbound_)
    {
        if (!tracked.used || tracked.msg_id != msg_id)
        {
            continue;
        }
        if (match != nullptr)
        {
            return nullptr;
        }
        match = &tracked;
    }
    return match;
}

ChatMessageLedger::TrackedOutbound*
ChatMessageLedger::findTrackedOutboundForProtocol(MessageId msg_id,
                                                  MeshProtocol protocol)
{
    if (msg_id == 0)
    {
        return nullptr;
    }
    for (TrackedOutbound& tracked : tracked_outbound_)
    {
        if (tracked.used && tracked.msg_id == msg_id &&
            tracked.protocol == protocol)
        {
            return &tracked;
        }
    }
    return nullptr;
}

void ChatMessageLedger::clearTrackedOutbound(
    const ConversationId& conversation)
{
    for (TrackedOutbound& tracked : tracked_outbound_)
    {
        if (tracked.used && tracked.conversation == conversation)
        {
            tracked = TrackedOutbound{};
        }
    }
}

void ChatMessageLedger::removePendingStatus(MessageId msg_id,
                                            MeshProtocol protocol)
{
    for (PendingStatusWrite& pending : pending_status_writes_)
    {
        if (pending.used && pending.msg_id == msg_id &&
            pending.protocol == protocol)
        {
            pending = PendingStatusWrite{};
        }
    }
}

void ChatMessageLedger::applyPendingStatus(ChatMessage& message) const
{
    for (const PendingStatusWrite& pending : pending_status_writes_)
    {
        if (pending.used && pending.msg_id == message.msg_id &&
            pending.protocol == message.protocol)
        {
            message.status = pending.status;
        }
    }
}

int ChatMessageLedger::oldestPendingOutboundIndex() const
{
    int oldest = -1;
    for (std::size_t index = 0; index < pending_outbound_writes_.size(); ++index)
    {
        const PendingOutboundWrite& pending = pending_outbound_writes_[index];
        if (!pending.used ||
            (oldest >= 0 &&
             pending.sequence >=
                 pending_outbound_writes_[static_cast<std::size_t>(oldest)]
                     .sequence))
        {
            continue;
        }
        oldest = static_cast<int>(index);
    }
    return oldest;
}

int ChatMessageLedger::oldestPendingStatusIndex() const
{
    int oldest = -1;
    for (std::size_t index = 0; index < pending_status_writes_.size(); ++index)
    {
        const PendingStatusWrite& pending = pending_status_writes_[index];
        if (!pending.used ||
            (oldest >= 0 &&
             pending.sequence >=
                 pending_status_writes_[static_cast<std::size_t>(oldest)]
                     .sequence))
        {
            continue;
        }
        oldest = static_cast<int>(index);
    }
    return oldest;
}

uint32_t ChatMessageLedger::nextPendingSequence()
{
    const uint32_t sequence = next_pending_sequence_++;
    if (next_pending_sequence_ == 0)
    {
        next_pending_sequence_ = 1;
    }
    return sequence;
}

void ChatMessageLedger::publishDeliveryEvent(const ChatMessage& message,
                                             MessageStatus status,
                                             uint32_t timestamp_ms,
                                             SendFailureKind failure)
{
    if (delivery_event_port_ == nullptr || message.msg_id == 0 ||
        !ChatOutboxService::isOutboundStatusUpdate(status))
    {
        return;
    }

    delivery_event_port_->publishDeliveryEvent(
        makeChatSendResultDeliveryEvent(
            toDeliveryRef(message),
            ChatOutboxService::toDeliveryState(status),
            status == MessageStatus::Failed
                ? failure
                : ChatOutboxService::failureForStatus(status),
            timestamp_ms));
}

void ChatMessageLedger::publishDeliveryEventForProtocol(
    MessageId msg_id,
    MeshProtocol protocol,
    MessageStatus status,
    uint32_t timestamp_ms,
    SendFailureKind failure)
{
    if (delivery_event_port_ == nullptr || msg_id == 0 ||
        !ChatOutboxService::isOutboundStatusUpdate(status))
    {
        return;
    }
    ChatDeliveryRef ref{};
    ref.protocol_id = msg_id;
    ref.protocol = static_cast<uint8_t>(protocol);
    delivery_event_port_->publishDeliveryEvent(
        makeChatSendResultDeliveryEvent(
            ref,
            ChatOutboxService::toDeliveryState(status),
            status == MessageStatus::Failed
                ? failure
                : ChatOutboxService::failureForStatus(status),
            timestamp_ms));
}

} // namespace chat::delivery
