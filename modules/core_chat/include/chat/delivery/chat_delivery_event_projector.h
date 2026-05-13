#pragma once

#include "chat/delivery/chat_delivery_read_model.h"

namespace chat::delivery
{

enum class SendFailureKind : uint8_t
{
    None,
    PeerKeyMissing,
    LocalIdentityMissing,
    RadioSendFailed,
    AckTimeout,
    UnsupportedProtocol,
    Rejected,
    Unknown,
};

class ChatDeliveryEventProjector
{
  public:
    explicit ChatDeliveryEventProjector(ChatDeliveryReadModel& read_model);

    void onQueued(ChatDeliveryRef ref, uint32_t now_ms);
    void onSending(ChatDeliveryRef ref, uint32_t now_ms);
    void onSent(ChatDeliveryRef ref, uint32_t now_ms);
    void onDelivered(ChatDeliveryRef ref, uint32_t now_ms);
    void onFailed(ChatDeliveryRef ref,
                  SendFailureKind failure,
                  uint32_t now_ms);
    void onReceived(ChatDeliveryRef ref, uint32_t now_ms);

  private:
    void project(ChatDeliveryRef ref,
                 DeliveryState state,
                 DeliveryFailureKind failure,
                 uint32_t now_ms);

    ChatDeliveryReadModel& read_model_;
};

DeliveryFailureKind toDeliveryFailureKind(SendFailureKind failure);

} // namespace chat::delivery
