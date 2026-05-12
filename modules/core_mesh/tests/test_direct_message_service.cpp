#include "fake/fake_mesh_dependencies.h"
#include "mesh/usecase/direct_message_service.h"
#include "mesh/usecase/peer_identity_service.h"

#include <cassert>

namespace
{

struct Harness
{
    mesh::tests::FakeClock clock;
    mesh::tests::FakeCryptoProvider crypto;
    mesh::tests::FakeLocalIdentityStore local_store;
    mesh::tests::FakePeerKeyStore peer_store;
    mesh::tests::FakePacketRadio radio;
    mesh::tests::FakeMeshEventSink events;
    mesh::tests::FakeProtocolStrategy protocol;
    mesh::PeerIdentityService identity{local_store, peer_store, crypto, clock};
    mesh::DirectMessageService direct{protocol, identity, radio, clock, events};
};

} // namespace

int main()
{
    Harness ok;
    ok.local_store.identity = mesh::tests::makeIdentity();
    ok.local_store.has_identity = true;
    ok.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));

    const uint8_t payload[] = {1, 2, 3};
    mesh::DirectMessageCommand command{mesh::NodeId{0xAA01},
                                       mesh::ByteView{payload, sizeof(payload)},
                                       true};
    auto sent = ok.direct.sendDirect(command);
    assert(sent.ok);
    assert(ok.protocol.build_count == 1);
    assert(ok.radio.send_count == 1);
    assert(ok.events.count(mesh::MeshEventKind::MessageSent) == 1);

    Harness missing_peer;
    missing_peer.local_store.identity = mesh::tests::makeIdentity();
    missing_peer.local_store.has_identity = true;
    auto peer_missing = missing_peer.direct.sendDirect(command);
    assert(!peer_missing.ok);
    assert(peer_missing.failure == mesh::SendFailure::PeerKeyMissing);
    assert(missing_peer.events.count(mesh::MeshEventKind::PeerKeyMissing) == 1);

    Harness build_fail;
    build_fail.local_store.identity = mesh::tests::makeIdentity();
    build_fail.local_store.has_identity = true;
    build_fail.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    build_fail.protocol.build_result =
        mesh::ProtocolResult::fail(mesh::ProtocolFailure::EncodeFailed);
    auto build_result = build_fail.direct.sendDirect(command);
    assert(!build_result.ok);
    assert(build_result.failure == mesh::SendFailure::PacketBuildFailed);
    assert(build_fail.radio.send_count == 0);
    assert(build_fail.events.count(mesh::MeshEventKind::SendFailed) == 1);

    Harness radio_fail;
    radio_fail.local_store.identity = mesh::tests::makeIdentity();
    radio_fail.local_store.has_identity = true;
    radio_fail.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    radio_fail.radio.send_result = mesh::RadioResult::fail(mesh::RadioFailure::Busy);
    auto radio_result = radio_fail.direct.sendDirect(command);
    assert(!radio_result.ok);
    assert(radio_result.failure == mesh::SendFailure::RadioSendFailed);
    assert(radio_fail.events.count(mesh::MeshEventKind::RadioError) == 1);
    return 0;
}
