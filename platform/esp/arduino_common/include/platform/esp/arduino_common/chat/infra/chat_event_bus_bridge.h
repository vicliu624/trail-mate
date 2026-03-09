#pragma once

#include "chat/usecase/chat_service.h"

namespace chat::infra
{

class ChatEventBusBridge : public chat::ChatService::IncomingMessageObserver
{
  public:
    explicit ChatEventBusBridge(chat::ChatService& service);
    ~ChatEventBusBridge() override;

    void onIncomingMessage(const chat::ChatMessage& msg, const chat::RxMeta* rx_meta) override;

  private:
    chat::ChatService& service_;
};

} // namespace chat::infra
