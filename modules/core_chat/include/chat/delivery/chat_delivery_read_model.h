#pragma once

#include "chat/delivery/chat_delivery_types.h"

#include <stddef.h>

namespace chat::delivery
{

class ChatDeliveryReadModel
{
  public:
    static constexpr size_t kMaxRecords = 32;

    bool upsert(const ChatDeliveryRecord& record);
    bool find(const ChatDeliveryRef& ref, ChatDeliveryRecord& out) const;
    void clear(const ChatDeliveryRef& ref);
    void clearAll();

    size_t size() const;

  private:
    ChatDeliveryRecord records_[kMaxRecords]{};
    size_t count_ = 0;
};

} // namespace chat::delivery
