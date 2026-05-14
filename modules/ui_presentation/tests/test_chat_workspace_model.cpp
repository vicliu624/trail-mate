#include "fake/fake_chat_action_sink.h"
#include "fake/fake_chat_presentation_source.h"
#include "ui_presentation/chat/chat_workspace_model.h"
#include "ui_presentation/common/fixed_text.h"

#include <cassert>
#include <cstring>

namespace
{

ui::chat::ConversationId directPeer(uint32_t peer, uint32_t channel)
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::DirectPeer;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = peer;
    id.secondary = channel;
    return id;
}

ui::chat::ConversationId team(uint32_t team_id)
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Team;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = team_id;
    return id;
}

} // namespace

int main()
{
    ui::chat::ChatWorkspaceSnapshot reset_probe;
    reset_probe.header.valid = true;
    reset_probe.conversation_count = 1;
    reset_probe.conversations[0].last_delivery =
        ui::chat::MessageDeliveryState::Delivered;
    reset_probe.message_count = 1;
    reset_probe.messages[0].ref.origin = ui::chat::MessageOrigin::RemoteStored;
    reset_probe.messages[0].delivery = ui::chat::MessageDeliveryState::Delivered;
    ui::copyText(reset_probe.messages[0].text, "stale");
    ui::chat::resetChatWorkspaceSnapshot(reset_probe);
    assert(!reset_probe.header.valid);
    assert(reset_probe.conversation_count == 0);
    assert(reset_probe.conversations[0].last_delivery ==
           ui::chat::MessageDeliveryState::Unknown);
    assert(reset_probe.message_count == 0);
    assert(reset_probe.messages[0].ref.origin ==
           ui::chat::MessageOrigin::SystemGenerated);
    assert(reset_probe.messages[0].delivery ==
           ui::chat::MessageDeliveryState::Unknown);
    assert(reset_probe.messages[0].text.empty());

    ui::tests::FakeChatPresentationSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.header.version = 6;
    source.snapshot_value.conversation_count = 2;
    source.snapshot_value.conversations[0].id = directPeer(1234, 0);
    source.snapshot_value.conversations[0].kind =
        ui::chat::ConversationKind::DirectPeer;
    source.snapshot_value.conversations[0].protocol =
        ui::chat::ChatProtocolKind::Meshtastic;
    ui::copyText(source.snapshot_value.conversations[0].title, "Ada");
    source.snapshot_value.conversations[1].id = team(1234);
    source.snapshot_value.conversations[1].kind = ui::chat::ConversationKind::Team;
    source.snapshot_value.conversations[1].protocol =
        ui::chat::ChatProtocolKind::TrailMate;
    ui::copyText(source.snapshot_value.conversations[1].title, "Field Team");
    source.snapshot_value.message_count = 1;
    source.snapshot_value.messages[0].conversation = directPeer(1234, 0);
    source.snapshot_value.messages[0].ref.origin =
        ui::chat::MessageOrigin::RemoteStored;
    source.snapshot_value.messages[0].ref.local_id = 42;
    ui::copyText(source.snapshot_value.messages[0].text, "hello");
    ui::copyText(source.snapshot_value.workspace_title, "Chat");

    ui::tests::FakeChatActionSink sink;
    ui::chat::ChatWorkspaceModel model(source, sink);

    model.setConversationOffset(3);
    model.setMessageOffset(5);

    const auto initial = model.snapshot();
    assert(initial.header.valid);
    assert(initial.header.version == 6);
    assert(initial.conversation_count == 2);
    assert(initial.conversations[0].id != initial.conversations[1].id);
    assert(std::strcmp(initial.conversations[0].title.c_str(), "Ada") == 0);
    assert(std::strcmp(initial.messages[0].text.c_str(), "hello") == 0);
    assert(source.build_count == 1);
    assert(source.last_request.conversation_offset == 3);
    assert(source.last_request.message_offset == 5);

    ui::chat::ChatWorkspaceSnapshot reusable_snapshot;
    assert(model.buildSnapshot(reusable_snapshot));
    assert(reusable_snapshot.header.valid);
    assert(reusable_snapshot.header.version == 6);
    assert(reusable_snapshot.conversation_count == 2);
    assert(source.build_count == 2);

    const auto ada = directPeer(1234, 0);
    const auto selected = model.selectConversation(ada);
    assert(selected.ok);
    assert(sink.select_count == 1);
    assert(sink.last_selected == ada);
    assert(model.selectedConversation() == ada);
    assert(model.messageOffset() == 0);

    const auto selected_snapshot = model.snapshot();
    assert(selected_snapshot.header.valid);
    assert(selected_snapshot.selected_conversation == ada);
    assert(source.last_request.selected == ada);

    const auto sent = model.sendMessage("on my way");
    assert(sent.ok);
    assert(sink.send_count == 1);
    assert(sink.last_message.conversation == ada);
    assert(std::strcmp(sink.last_message.text, "on my way") == 0);
    assert(sink.last_message.text_len == 9);

    const auto marked = model.markRead(ada);
    assert(marked.ok);
    assert(sink.mark_read_count == 1);
    assert(sink.last_mark_read == ada);

    const auto team_id = team(1234);
    assert(ada != team_id);
    const auto team_selected = model.selectConversation(team_id);
    assert(team_selected.ok);
    assert(model.selectedConversation() == team_id);
    assert(model.selectedConversation() != ada);

    ui::chat::ChatWorkspaceModel empty_model(source, sink);
    const auto no_selection_send = empty_model.sendMessage("nope");
    assert(!no_selection_send.ok);
    assert(no_selection_send.failure == ui::UiActionFailure::InvalidInput);

    const auto empty_text = model.sendMessage("");
    assert(!empty_text.ok);
    assert(empty_text.failure == ui::UiActionFailure::InvalidInput);

    const auto invalid_select = model.selectConversation({});
    assert(!invalid_select.ok);
    assert(invalid_select.failure == ui::UiActionFailure::InvalidInput);

    const auto invalid_mark = model.markRead({});
    assert(!invalid_mark.ok);
    assert(invalid_mark.failure == ui::UiActionFailure::InvalidInput);

    const auto before_rejected = model.selectedConversation();
    sink.select_result = ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    const auto rejected = model.selectConversation(ada);
    assert(!rejected.ok);
    assert(rejected.failure == ui::UiActionFailure::Rejected);
    assert(model.selectedConversation() == ada);
    assert(model.selectedConversation() != before_rejected);

    source.available = false;
    const auto invalid_snapshot = model.snapshot();
    assert(!invalid_snapshot.header.valid);

    reusable_snapshot.header.valid = true;
    assert(!model.buildSnapshot(reusable_snapshot));
    assert(!reusable_snapshot.header.valid);

    return 0;
}
