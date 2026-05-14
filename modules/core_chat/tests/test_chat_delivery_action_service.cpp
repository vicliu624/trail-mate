#include "chat/delivery/chat_delivery_action_service.h"

#include <cassert>

namespace
{

chat::delivery::ChatDeliveryRef ref(uint32_t id)
{
    chat::delivery::ChatDeliveryRef out{};
    out.protocol_id = id;
    return out;
}

chat::delivery::ChatDeliveryRecord record(
    uint32_t id,
    chat::delivery::DeliveryState state,
    chat::delivery::DeliveryFailureKind failure =
        chat::delivery::DeliveryFailureKind::None)
{
    chat::delivery::ChatDeliveryRecord out{};
    out.ref = ref(id);
    out.state = state;
    out.failure = failure;
    out.updated_at_ms = id * 10U;
    return out;
}

chat::delivery::ChatDeliveryActionRequest request(
    chat::delivery::ChatDeliveryActionKind kind,
    uint32_t id)
{
    chat::delivery::ChatDeliveryActionRequest out{};
    out.kind = kind;
    out.ref = ref(id);
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
        return result;
    }

    int call_count = 0;
    chat::delivery::ChatDeliveryRef last_ref{};
    chat::delivery::ChatDeliveryActionResult result =
        chat::delivery::ChatDeliveryActionResult::success();
};

} // namespace

int main()
{
    using namespace chat::delivery;

    ChatDeliveryReadModel read_model;
    ChatDeliveryActionService service(read_model);

    ChatDeliveryActionRequest invalid{};
    invalid.kind = ChatDeliveryActionKind::ClearFailure;
    auto result = service.handleDeliveryAction(invalid);
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::InvalidRef);

    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::ClearFailure, 100));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::NotFound);

    assert(read_model.upsert(record(101,
                                    DeliveryState::Failed,
                                    DeliveryFailureKind::AckTimeout)));
    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::ClearFailure, 101));
    assert(result.ok);
    ChatDeliveryRecord found{};
    assert(!read_model.find(ref(101), found));

    assert(read_model.upsert(record(102, DeliveryState::Sent)));
    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::ClearFailure, 102));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::NotRetryable);
    assert(read_model.find(ref(102), found));

    assert(read_model.upsert(record(103, DeliveryState::Queued)));
    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::CancelPending, 103));
    assert(result.ok);
    assert(!read_model.find(ref(103), found));

    assert(read_model.upsert(record(104, DeliveryState::Sending)));
    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::CancelPending, 104));
    assert(result.ok);
    assert(!read_model.find(ref(104), found));

    assert(read_model.upsert(record(105, DeliveryState::Delivered)));
    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::CancelPending, 105));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::NotRetryable);
    assert(read_model.find(ref(105), found));

    result = service.handleDeliveryAction(
        request(ChatDeliveryActionKind::Retry, 106));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::Unsupported);

    FakeRetryPort retry_port;
    ChatDeliveryActionService retrying_service(read_model, &retry_port);
    result = retrying_service.handleDeliveryAction(
        request(ChatDeliveryActionKind::Retry, 107));
    assert(result.ok);
    assert(retry_port.call_count == 1);
    assert(retry_port.last_ref == ref(107));

    retry_port.result =
        ChatDeliveryActionResult::fail(ChatDeliveryActionFailure::Rejected);
    result = retrying_service.handleDeliveryAction(
        request(ChatDeliveryActionKind::Retry, 108));
    assert(!result.ok);
    assert(result.failure == ChatDeliveryActionFailure::Rejected);
    assert(retry_port.call_count == 2);
    assert(retry_port.last_ref == ref(108));

    return 0;
}
