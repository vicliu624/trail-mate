#include "fake/fake_mesh_dependencies.h"
#include "mesh/usecase/mesh_dedup_service.h"
#include "mesh/usecase/peer_identity_service.h"
#include "mesh/usecase/receive_packet_service.h"

#include <cassert>

namespace
{

struct Harness
{
    mesh::tests::FakeClock clock;
    mesh::tests::FakeCryptoProvider crypto;
    mesh::tests::FakeLocalIdentityStore local_store;
    mesh::tests::FakePeerKeyStore peer_store;
    mesh::tests::FakeMeshEventSink events;
    mesh::tests::FakeProtocolStrategy protocol;
    mesh::MeshDedupService dedup;
    mesh::PeerIdentityService identity{local_store, peer_store, crypto, clock};
    mesh::ReceivePacketService receive{protocol, identity, events, clock, &dedup};
};

} // namespace

int main()
{
    mesh::RadioRxPacket packet{};
    packet.bytes[0] = 1;
    packet.size = 1;

    Harness message;
    message.protocol.next_event.kind = mesh::MeshProtocolEventKind::MessageReceived;
    message.protocol.next_event.peer = mesh::NodeId{0x1101};
    message.receive.onRadioPacket(packet);
    assert(message.events.count(mesh::MeshEventKind::MessageReceived) == 1);

    Harness discovered;
    discovered.protocol.next_event.kind = mesh::MeshProtocolEventKind::PeerDiscovered;
    discovered.protocol.next_event.peer = mesh::NodeId{0x1201};
    discovered.receive.onRadioPacket(packet);
    assert(discovered.events.count(mesh::MeshEventKind::PeerDiscovered) == 1);

    Harness learned;
    learned.clock.now_ms = 500;
    learned.protocol.next_event.kind = mesh::MeshProtocolEventKind::PeerKeyLearned;
    learned.protocol.next_event.peer = mesh::NodeId{0x2201};
    learned.protocol.next_event.peer_key = mesh::tests::makePeerKey(0x2201, false);
    learned.protocol.next_event.peer_key.updated_at_ms = 0;

    learned.receive.onRadioPacket(packet);
    assert(learned.events.count(mesh::MeshEventKind::PeerKeyLearned) == 1);

    mesh::PeerPublicKey key{};
    assert(learned.peer_store.get(mesh::NodeId{0x2201}, key).ok);
    assert(key.updated_at_ms == 500);

    Harness protocol_error;
    protocol_error.protocol.parse_result =
        mesh::ProtocolResult::fail(mesh::ProtocolFailure::DecodeFailed);
    protocol_error.receive.onRadioPacket(packet);
    assert(protocol_error.events.count(mesh::MeshEventKind::ProtocolError) == 1);

    Harness duplicate;
    duplicate.protocol.next_event.kind = mesh::MeshProtocolEventKind::MessageReceived;
    duplicate.protocol.next_event.peer = mesh::NodeId{0x3301};
    duplicate.protocol.next_event.packet_id = 77;
    duplicate.receive.onRadioPacket(packet);
    duplicate.receive.onRadioPacket(packet);
    assert(duplicate.events.count(mesh::MeshEventKind::MessageReceived) == 1);
    return 0;
}
