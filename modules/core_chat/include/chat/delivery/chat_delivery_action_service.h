#pragma once

#include "chat/delivery/chat_delivery_action_sink.h"
#include "chat/delivery/chat_delivery_read_model.h"

namespace chat::delivery
{

class IRetryChatMessagePort
{
  public:
    virtual ~IRetryChatMessagePort() = default;

    virtual ChatDeliveryActionResult retryMessage(ChatDeliveryRef ref) = 0;
};

class ChatDeliveryActionService final : public IChatDeliveryActionSink
{
  public:
    ChatDeliveryActionService(ChatDeliveryReadModel& read_model,
                              IRetryChatMessagePort* retry_port = nullptr);

    ChatDeliveryActionResult handleDeliveryAction(
        const ChatDeliveryActionRequest& request) override;

  private:
    ChatDeliveryActionResult retry(ChatDeliveryRef ref);
    ChatDeliveryActionResult cancelPending(ChatDeliveryRef ref);
    ChatDeliveryActionResult clearFailure(ChatDeliveryRef ref);

    ChatDeliveryReadModel& read_model_;
    IRetryChatMessagePort* retry_port_ = nullptr;
};

} // namespace chat::delivery
