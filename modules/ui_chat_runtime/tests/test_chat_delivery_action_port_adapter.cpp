#include "chat/delivery/chat_delivery_action_service.h"
#include "ui_chat_runtime/chat_delivery_action_port_adapter.h"

#include <cassert>

namespace
{

ui::chat::MessageRef messageRef(uint32_t id)
{
    ui::chat::MessageRef out{};
    out.origin = ui::chat::MessageOrigin::LocalStored;
    out.protocol_id = id;
    return out;
}

chat::delivery::ChatDeliveryRecord deliveryRecord(
    uint32_t id,
    chat::delivery::DeliveryState state,
    chat::delivery::DeliveryFailureKind failure =
        chat::delivery::DeliveryFailureKind::None)
{
    chat::delivery::ChatDeliveryRecord out{};
    out.ref.protocol_id = id;
    out.state = state;
    out.failure = failure;
    return out;
}

class FakeRetryPort final : public chat::delivery::IRetryChatMessagePort
{
  public:
    chat::delivery::ChatDeliveryActionResult retryMessage(
        chat::delivery::ChatDeliveryRef ref) override
    {
        ++call_count;
        last_ref = ref;
        return chat::delivery::ChatDeliveryActionResult::success();
    }

    int call_count = 0;
    chat::delivery::ChatDeliveryRef last_ref{};
};

} // namespace

int main()
{
    using namespace chat::delivery;

    ChatDeliveryReadModel read_model;
    ChatDeliveryActionService action_service(read_model);
    ui_chat_runtime::ChatDeliveryActionPortAdapter adapter(action_service);

    const auto mapped = ui_chat_runtime::toDeliveryRef(messageRef(700));
    assert(mapped.local_id == 0);
    assert(mapped.protocol_id == 700);
    assert(mapped.nonce_or_seq == 0);

    ui::chat::MessageRef invalid{};
    auto result = adapter.clearFailure(invalid);
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::InvalidRef);

    assert(read_model.upsert(deliveryRecord(701,
                                            DeliveryState::Failed,
                                            DeliveryFailureKind::Rejected)));
    result = adapter.clearFailure(messageRef(701));
    assert(result.ok);
    ChatDeliveryRecord found{};
    assert(!read_model.find(ChatDeliveryRef{0, 701, 0}, found));

    assert(read_model.upsert(deliveryRecord(702, DeliveryState::Queued)));
    result = adapter.cancelPending(messageRef(702));
    assert(result.ok);
    assert(!read_model.find(ChatDeliveryRef{0, 702, 0}, found));

    assert(read_model.upsert(deliveryRecord(703, DeliveryState::Sent)));
    result = adapter.cancelPending(messageRef(703));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::NotRetryable);
    assert(read_model.find(ChatDeliveryRef{0, 703, 0}, found));

    result = adapter.retryMessage(messageRef(704));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::Unsupported);

    FakeRetryPort retry_port;
    ChatDeliveryActionService retrying_service(read_model, &retry_port);
    ui_chat_runtime::ChatDeliveryActionPortAdapter retrying_adapter(
        retrying_service);
    result = retrying_adapter.retryMessage(messageRef(705));
    assert(result.ok);
    assert(retry_port.call_count == 1);
    const ChatDeliveryRef expected_retry_ref{0, 705, 0};
    assert(retry_port.last_ref == expected_retry_ref);

    result = retrying_adapter.handleMessageAction(
        ChatDeliveryActionKind::ClearFailure,
        messageRef(706));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::NotFound);

    return 0;
}
