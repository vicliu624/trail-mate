#include "fake/fake_mesh_dependencies.h"
#include "mesh/usecase/direct_message_service.h"
#include "mesh/usecase/mesh_session.h"
#include "mesh/usecase/peer_identity_service.h"
#include "mesh/usecase/receive_packet_service.h"

#include <cassert>

int main()
{
    mesh::tests::FakeClock clock;
    mesh::tests::FakeCryptoProvider crypto;
    mesh::tests::FakeLocalIdentityStore local_store;
    mesh::tests::FakePeerKeyStore peer_store;
    mesh::tests::FakePacketRadio radio;
    mesh::tests::FakeMeshEventSink events;
    mesh::tests::FakeProtocolStrategy protocol;
    mesh::PeerIdentityService identity(local_store, peer_store, crypto, clock);
    mesh::ReceivePacketService receive(protocol, identity, events, clock);
    mesh::DirectMessageService direct(protocol, identity, radio, clock, events);
    mesh::MeshSession session(radio, protocol, direct, receive, clock);

    mesh::MeshRuntimeConfig config{};
    config.protocol = mesh::MeshProtocolKind::Meshtastic;
    config.radio.frequency_hz = 915000000;
    config.radio.spreading_factor = 7;
    assert(session.start(config));
    assert(session.state() == mesh::MeshSessionState::Ready);
    assert(radio.configure_count == 1);
    assert(radio.last_config.frequency_hz == 915000000);

    protocol.next_event.kind = mesh::MeshProtocolEventKind::MessageReceived;
    protocol.next_event.peer = mesh::NodeId{0x4401};
    mesh::RadioRxPacket rx{};
    rx.size = 1;
    rx.bytes[0] = 0x42;
    radio.rx_packets.push_back(rx);
    session.tick();
    assert(events.count(mesh::MeshEventKind::MessageReceived) == 1);

    local_store.identity = mesh::tests::makeIdentity();
    local_store.has_identity = true;
    peer_store.keys.push_back(mesh::tests::makePeerKey(0x4401));
    const uint8_t payload[] = {0x10, 0x11};
    mesh::DirectMessageCommand command{mesh::NodeId{0x4401},
                                       mesh::ByteView{payload, sizeof(payload)},
                                       true};
    assert(session.sendDirect(command).ok);
    assert(radio.send_count == 1);

    session.stop();
    assert(!session.sendDirect(command).ok);
    return 0;
}
