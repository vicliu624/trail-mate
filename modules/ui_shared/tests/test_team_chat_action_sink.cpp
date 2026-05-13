#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/team_chat_action_sink.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace
{

class FakeTeamChatCommandPort final
    : public ui::presentation_sources::ITeamChatCommandPort
{
  public:
    bool setKeysFromPsk(const ::team::TeamId& team_id,
                        uint32_t key_id,
                        const uint8_t* psk,
                        size_t psk_len) override
    {
        ++set_keys_count;
        last_team_id = team_id;
        last_key_id = key_id;
        last_psk_len = psk_len;
        return psk != nullptr && set_keys_ok;
    }

    bool sendTeamChat(const ::team::proto::TeamChatMessage& message,
                      uint8_t team_channel_raw) override
    {
        ++send_count;
        last_message = message;
        last_channel_raw = team_channel_raw;
        return send_ok;
    }

    bool set_keys_ok = true;
    bool send_ok = true;
    int set_keys_count = 0;
    int send_count = 0;
    ::team::TeamId last_team_id{};
    uint32_t last_key_id = 0;
    size_t last_psk_len = 0;
    ::team::proto::TeamChatMessage last_message;
    uint8_t last_channel_raw = 0;
};

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0x42;
    id[1] = 0x43;
    id[2] = 0x44;
    id[3] = 0x45;
    return id;
}

team::ui::TeamUiSnapshot makeSnapshot(bool has_psk = true)
{
    team::ui::TeamUiSnapshot snap;
    snap.has_team_id = true;
    snap.team_id = testTeamId();
    snap.team_name = "Bravo";
    snap.security_round = 9;
    snap.has_team_psk = has_psk;
    snap.team_chat_unread = 4;
    for (size_t i = 0; i < snap.team_psk.size(); ++i)
    {
        snap.team_psk[i] = static_cast<uint8_t>(i + 1U);
    }
    return snap;
}

ui::chat::ConversationId teamConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Team;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = 1;
    return id;
}

ui::chat::ConversationId directConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::DirectPeer;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = 1234;
    return id;
}

ui::chat::ConversationId channelConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Channel;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = 1;
    return id;
}

ui::chat::SendMessageView sendView(ui::chat::ConversationId id,
                                   const char* text)
{
    ui::chat::SendMessageView view;
    view.conversation = id;
    view.text = text;
    view.text_len = text == nullptr ? 0U : std::strlen(text);
    return view;
}

} // namespace

int main()
{
    team::ui::ITeamUiStore& store = team::ui::team_ui_get_store();
    store.clear();
    store.save(makeSnapshot());

    FakeTeamChatCommandPort fake_port;
    ui::presentation_sources::TeamChatActionSink sink(store, &fake_port, 7);

    assert(sink.selectConversation(teamConversation()).ok);
    const auto direct_select = sink.selectConversation(directConversation());
    assert(!direct_select.ok);
    assert(direct_select.failure == ui::UiActionFailure::Unsupported);

    const auto direct_send = sink.sendMessage(sendView(directConversation(), "x"));
    assert(!direct_send.ok);
    assert(direct_send.failure == ui::UiActionFailure::Unsupported);

    const auto channel_send = sink.sendMessage(sendView(channelConversation(), "x"));
    assert(!channel_send.ok);
    assert(channel_send.failure == ui::UiActionFailure::Unsupported);

    const auto empty_send = sink.sendMessage(sendView(teamConversation(), ""));
    assert(!empty_send.ok);
    assert(empty_send.failure == ui::UiActionFailure::InvalidInput);

    ui::presentation_sources::TeamChatActionSink no_port_sink(store, nullptr);
    const auto no_port_send =
        no_port_sink.sendMessage(sendView(teamConversation(), "no controller"));
    assert(!no_port_send.ok);
    assert(no_port_send.failure == ui::UiActionFailure::NotReady);

    store.save(makeSnapshot(false));
    const auto no_psk_send =
        sink.sendMessage(sendView(teamConversation(), "no psk"));
    assert(!no_psk_send.ok);
    assert(no_psk_send.failure == ui::UiActionFailure::NotReady);

    store.save(makeSnapshot());
    const auto sent = sink.sendMessage(sendView(teamConversation(), "team hello"));
    assert(sent.ok);
    assert(fake_port.set_keys_count == 1);
    assert(fake_port.last_team_id == testTeamId());
    assert(fake_port.last_key_id == 9);
    assert(fake_port.last_psk_len == team::proto::kTeamChannelPskSize);
    assert(fake_port.send_count == 1);
    assert(fake_port.last_channel_raw == 7);
    assert(fake_port.last_message.header.type ==
           team::proto::TeamChatType::Text);
    assert(std::string(fake_port.last_message.payload.begin(),
                       fake_port.last_message.payload.end()) == "team hello");

    std::vector<team::ui::TeamChatLogEntry> entries;
    assert(team::ui::team_ui_chatlog_load_recent(testTeamId(), 4, entries));
    assert(entries.size() == 1);
    assert(!entries[0].incoming);
    assert(entries[0].type == team::proto::TeamChatType::Text);
    assert(std::string(entries[0].payload.begin(), entries[0].payload.end()) ==
           "team hello");

    team::ui::TeamUiSnapshot snap;
    assert(store.load(snap));
    assert(snap.team_chat_unread == 4);
    assert(sink.markRead(teamConversation()).ok);
    assert(store.load(snap));
    assert(snap.team_chat_unread == 0);

    const auto mark_direct = sink.markRead(directConversation());
    assert(!mark_direct.ok);
    assert(mark_direct.failure == ui::UiActionFailure::Unsupported);

    fake_port.send_ok = false;
    const auto rejected = sink.sendMessage(sendView(teamConversation(), "reject"));
    assert(!rejected.ok);
    assert(rejected.failure == ui::UiActionFailure::Rejected);

    store.clear();
    return 0;
}
