#include "chat/delivery/chat_delivery_read_model.h"

#include <cassert>

namespace
{

chat::delivery::ChatDeliveryRef ref(uint32_t id)
{
    chat::delivery::ChatDeliveryRef out{};
    out.protocol_id = id;
    return out;
}

} // namespace

int main()
{
    chat::delivery::ChatDeliveryReadModel model;
    assert(model.size() == 0);

    chat::delivery::ChatDeliveryRecord invalid{};
    invalid.state = chat::delivery::DeliveryState::Queued;
    assert(!model.upsert(invalid));
    assert(model.size() == 0);

    chat::delivery::ChatDeliveryRecord queued{};
    queued.ref = ref(10);
    queued.state = chat::delivery::DeliveryState::Queued;
    queued.updated_at_ms = 100;
    assert(model.upsert(queued));
    assert(model.size() == 1);

    chat::delivery::ChatDeliveryRecord found{};
    assert(model.find(ref(10), found));
    assert(found.state == chat::delivery::DeliveryState::Queued);
    assert(found.updated_at_ms == 100);

    queued.state = chat::delivery::DeliveryState::Sent;
    queued.updated_at_ms = 200;
    assert(model.upsert(queued));
    assert(model.size() == 1);
    assert(model.find(ref(10), found));
    assert(found.state == chat::delivery::DeliveryState::Sent);
    assert(found.updated_at_ms == 200);

    for (uint32_t id = 11; id < 11 + chat::delivery::ChatDeliveryReadModel::kMaxRecords;
         ++id)
    {
        chat::delivery::ChatDeliveryRecord record{};
        record.ref = ref(id);
        record.state = chat::delivery::DeliveryState::Sending;
        assert(model.upsert(record));
    }
    assert(model.size() == chat::delivery::ChatDeliveryReadModel::kMaxRecords);
    assert(!model.find(ref(10), found));

    model.clear(ref(20));
    assert(model.size() == chat::delivery::ChatDeliveryReadModel::kMaxRecords - 1);
    assert(!model.find(ref(20), found));

    model.clearAll();
    assert(model.size() == 0);
    return 0;
}
