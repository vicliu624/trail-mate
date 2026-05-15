#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "sys/event_bus.h"
#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

#include <cassert>
#include <string>

namespace
{

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    bool sendText(::chat::ChannelId,
                  const std::string&,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId = 0) override
    {
        if (out_msg_id != nullptr)
        {
            *out_msg_id = next_id++;
        }
        return send_ok;
    }

    bool pollIncomingText(::chat::MeshIncomingText*) override { return false; }
    bool sendAppData(::chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }
    bool pollIncomingData(::chat::MeshIncomingData*) override { return false; }
    void applyConfig(const ::chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }

    bool send_ok = true;
    ::chat::MessageId next_id = 700;
};

} // namespace

int main()
{
    ::chat::ChatModel model;
    FakeMeshAdapter adapter;
    ::chat::RamStore store;
    ::chat::ChatService service(model, adapter, store);

    ::chat::delivery::ChatDeliveryReadModel read_model;
    ::chat::delivery::ChatDeliveryEventProjector projector(read_model);
    ::chat::delivery::ProjectingChatDeliveryEventPort event_port(projector);
    ::ui_chat_runtime::ChatDeliveryEventProjectionAdapter projection_adapter(
        service,
        event_port);

    const auto sent_id =
        service.sendText(::chat::ChannelId::PRIMARY, "queued", 0);
    assert(sent_id == 700);

    service.handleSendResult(sent_id, true);
    ::sys::ChatSendResultEvent sent_event(sent_id, true);
    sent_event.timestamp = 1234;
    projection_adapter.onChatSendResult(sent_event);

    ::chat::delivery::ChatDeliveryRecord record{};
    assert(read_model.find(::chat::delivery::ChatDeliveryRef{0, sent_id, 0},
                           record));
    assert(record.state == ::chat::delivery::DeliveryState::Sent);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 1234);

    const auto failed_id =
        service.sendText(::chat::ChannelId::PRIMARY, "fail", 0);
    assert(failed_id == 701);

    service.handleSendResult(failed_id, false);
    ::sys::ChatSendResultEvent failed_event(failed_id, false);
    failed_event.timestamp = 2345;
    projection_adapter.onChatSendResult(failed_event);

    assert(read_model.find(::chat::delivery::ChatDeliveryRef{0, failed_id, 0},
                           record));
    assert(record.state == ::chat::delivery::DeliveryState::Failed);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::Unknown);
    assert(record.updated_at_ms == 2345);

    const auto timeout_id =
        service.sendText(::chat::ChannelId::PRIMARY, "timeout", 0);
    assert(timeout_id == 702);
    projection_adapter.onAckTimeout(timeout_id, 3456);
    assert(read_model.find(::chat::delivery::ChatDeliveryRef{0, timeout_id, 0},
                           record));
    assert(record.state == ::chat::delivery::DeliveryState::Failed);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::AckTimeout);
    assert(record.updated_at_ms == 3456);

    projection_adapter.onAckTimeout(0, 4567);
    assert(read_model.size() == 3);

    ::sys::ChatSendResultEvent unknown_event(9999, false);
    projection_adapter.onChatSendResult(unknown_event);
    assert(read_model.size() == 3);

    return 0;
}
