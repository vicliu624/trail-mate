#include "chat/ports/i_mesh_adapter.h"
#include "ui_key_verification_runtime/key_verification_presentation_source.h"
#include <cassert>
#include <cstring>

struct Mesh final : chat::IMeshAdapter
{
    chat::MeshProtocol backendProtocol() const override { return chat::MeshProtocol::MeshCore; }
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, std::size_t, chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return false; }
    bool pollIncomingRawPacket(uint8_t*, std::size_t&, std::size_t) override { return false; }
};
int main()
{
    ui_key_verification_runtime::KeyVerificationSessionAdapter session;
    session.peer_id = 99;
    session.state = ui::key_verification::VerificationState::Pending;
    std::strcpy(session.verification_code, "old code");
    Mesh mesh;
    ui_key_verification_runtime::KeyVerificationPresentationSource source(session, nullptr, &mesh);
    source.onRuntimeFailure(99);
    ui::key_verification::KeyVerificationSnapshot snapshot;
    assert(source.buildKeyVerificationSnapshot({ui::key_verification::VerificationProtocol::Unknown, 99}, snapshot));
    assert(snapshot.protocol == ui::key_verification::VerificationProtocol::MeshCore);
    assert(snapshot.state == ui::key_verification::VerificationState::Failed);
    assert(snapshot.failure == ui::key_verification::VerificationFailureKind::RuntimeUnavailable);
    assert(snapshot.verification_code.empty() && !snapshot.can_copy_code && !snapshot.requires_number);
    assert(snapshot.can_refresh);
}
