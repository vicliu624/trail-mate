#pragma once

#include "chat/delivery/chat_delivery_event_projector.h"

namespace chat::delivery
{

struct ChatDeliveryEvent
{
    ChatDeliveryRef ref;
    DeliveryState state = DeliveryState::Unknown;
    SendFailureKind failure = SendFailureKind::None;
    uint32_t timestamp_ms = 0;
};

class IChatDeliveryEventPort
{
  public:
    virtual ~IChatDeliveryEventPort() = default;

    virtual void publishDeliveryEvent(const ChatDeliveryEvent& event) = 0;
};

class ProjectingChatDeliveryEventPort final : public IChatDeliveryEventPort
{
  public:
    explicit ProjectingChatDeliveryEventPort(
        ChatDeliveryEventProjector& projector);

    void publishDeliveryEvent(const ChatDeliveryEvent& event) override;

  private:
    ChatDeliveryEventProjector& projector_;
};

} // namespace chat::delivery
