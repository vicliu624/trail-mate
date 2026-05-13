#include "chat/delivery/chat_delivery_read_model.h"

namespace chat::delivery
{

bool ChatDeliveryReadModel::upsert(const ChatDeliveryRecord& record)
{
    if (!record.ref.isValid())
    {
        return false;
    }

    for (size_t i = 0; i < count_; ++i)
    {
        if (records_[i].ref == record.ref)
        {
            records_[i] = record;
            return true;
        }
    }

    if (count_ < kMaxRecords)
    {
        records_[count_++] = record;
        return true;
    }

    for (size_t i = 1; i < count_; ++i)
    {
        records_[i - 1] = records_[i];
    }
    records_[count_ - 1] = record;
    return true;
}

bool ChatDeliveryReadModel::find(const ChatDeliveryRef& ref,
                                 ChatDeliveryRecord& out) const
{
    if (!ref.isValid())
    {
        return false;
    }

    for (size_t i = 0; i < count_; ++i)
    {
        if (records_[i].ref == ref)
        {
            out = records_[i];
            return true;
        }
    }
    return false;
}

void ChatDeliveryReadModel::clear(const ChatDeliveryRef& ref)
{
    if (!ref.isValid())
    {
        return;
    }

    for (size_t i = 0; i < count_; ++i)
    {
        if (!(records_[i].ref == ref))
        {
            continue;
        }

        for (size_t j = i + 1; j < count_; ++j)
        {
            records_[j - 1] = records_[j];
        }
        --count_;
        records_[count_] = ChatDeliveryRecord{};
        return;
    }
}

void ChatDeliveryReadModel::clearAll()
{
    for (size_t i = 0; i < count_; ++i)
    {
        records_[i] = ChatDeliveryRecord{};
    }
    count_ = 0;
}

size_t ChatDeliveryReadModel::size() const
{
    return count_;
}

} // namespace chat::delivery
