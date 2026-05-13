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
    assert(ok.protocol.last_context.local_identity.valid);
    assert(ok.protocol.last_context.peer_key.node_id == mesh::NodeId{0xAA01});
    assert(ok.radio.send_count == 1);
    assert(ok.events.count(mesh::MeshEventKind::MessageSent) == 1);

    Harness channel_key_path;
    mesh::DirectMessageCommand channel_command{mesh::NodeId{0xAA02},
                                               mesh::ByteView{payload, sizeof(payload)},
                                               false};
    const uint8_t psk[] = {0x10, 0x11, 0x12, 0x13};
    channel_command.from = mesh::NodeId{0x1001};
    channel_command.application_port = 77;
    channel_command.packet_id = 0x12345678;
    channel_command.channel_hash = 0x2A;
    channel_command.channel_key = mesh::ByteView{psk, sizeof(psk)};
    channel_command.hop_limit = 5;
    channel_command.has_air_want_ack = true;
    channel_command.air_want_ack = false;
    channel_command.include_payload_dest = false;
    channel_command.require_local_identity = false;
    channel_command.require_peer_key = false;
    auto channel_sent = channel_key_path.direct.sendDirect(channel_command);
    assert(channel_sent.ok);
    assert(channel_key_path.crypto.random_count == 0);
    assert(channel_key_path.peer_store.keys.empty());
    assert(channel_key_path.protocol.last_context.local_node == mesh::NodeId{0x1001});
    assert(channel_key_path.protocol.last_context.packet_id == 0x12345678);
    assert(channel_key_path.protocol.last_context.channel_hash == 0x2A);
    assert(channel_key_path.protocol.last_context.channel_key.data == psk);
    assert(channel_key_path.protocol.last_context.channel_key.size == sizeof(psk));
    assert(channel_key_path.protocol.last_context.hop_limit == 5);
    assert(channel_key_path.protocol.last_context.has_air_want_ack);
    assert(!channel_key_path.protocol.last_context.air_want_ack);
    assert(!channel_key_path.protocol.last_context.include_payload_dest);
    assert(channel_key_path.radio.send_count == 1);

    Harness invalid;
    mesh::DirectMessageCommand invalid_command{mesh::NodeId{},
                                               mesh::ByteView{payload, sizeof(payload)},
                                               true};
    auto invalid_result = invalid.direct.sendDirect(invalid_command);
    assert(!invalid_result.ok);
    assert(invalid_result.failure == mesh::SendFailure::InvalidInput);
    assert(invalid.protocol.build_count == 0);
    assert(invalid.radio.send_count == 0);

    Harness missing_local;
    missing_local.crypto.random_result =
        mesh::CryptoResult::fail(mesh::CryptoFailure::InvalidInput);
    missing_local.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    auto local_missing = missing_local.direct.sendDirect(command);
    assert(!local_missing.ok);
    assert(local_missing.failure == mesh::SendFailure::LocalIdentityMissing);
    assert(missing_local.protocol.build_count == 0);
    assert(missing_local.events.count(mesh::MeshEventKind::SendFailed) == 1);

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

    Harness protocol_missing_peer;
    protocol_missing_peer.local_store.identity = mesh::tests::makeIdentity();
    protocol_missing_peer.local_store.has_identity = true;
    protocol_missing_peer.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    protocol_missing_peer.protocol.build_result =
        mesh::ProtocolResult::fail(mesh::ProtocolFailure::MissingPeerKey);
    auto protocol_peer_missing = protocol_missing_peer.direct.sendDirect(command);
    assert(!protocol_peer_missing.ok);
    assert(protocol_peer_missing.failure == mesh::SendFailure::PeerKeyMissing);
    assert(protocol_missing_peer.events.count(mesh::MeshEventKind::PeerKeyMissing) == 1);

    Harness crypto_fail;
    crypto_fail.local_store.identity = mesh::tests::makeIdentity();
    crypto_fail.local_store.has_identity = true;
    crypto_fail.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    crypto_fail.protocol.build_result =
        mesh::ProtocolResult::fail(mesh::ProtocolFailure::CryptoFailed);
    auto crypto_result = crypto_fail.direct.sendDirect(command);
    assert(!crypto_result.ok);
    assert(crypto_result.failure == mesh::SendFailure::CryptoFailed);

    Harness unsupported;
    unsupported.local_store.identity = mesh::tests::makeIdentity();
    unsupported.local_store.has_identity = true;
    unsupported.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    unsupported.protocol.build_result =
        mesh::ProtocolResult::fail(mesh::ProtocolFailure::Unsupported);
    auto unsupported_result = unsupported.direct.sendDirect(command);
    assert(!unsupported_result.ok);
    assert(unsupported_result.failure == mesh::SendFailure::ProtocolUnsupported);

    Harness radio_fail;
    radio_fail.local_store.identity = mesh::tests::makeIdentity();
    radio_fail.local_store.has_identity = true;
    radio_fail.peer_store.keys.push_back(mesh::tests::makePeerKey(0xAA01));
    radio_fail.radio.send_result = mesh::RadioResult::fail(mesh::RadioFailure::Busy);
    auto radio_result = radio_fail.direct.sendDirect(command);
    assert(!radio_result.ok);
    assert(radio_result.failure == mesh::SendFailure::RadioSendFailed);
    assert(radio_fail.events.count(mesh::MeshEventKind::RadioError) == 1);
    assert(radio_fail.events.count(mesh::MeshEventKind::SendFailed) == 1);
    return 0;
}
