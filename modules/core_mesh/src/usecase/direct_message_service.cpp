#include "mesh/usecase/direct_message_service.h"

namespace mesh
{

DirectMessageService::DirectMessageService(MeshProtocolStrategy& protocol,
                                           PeerIdentityService& identity,
                                           IPacketRadio& radio,
                                           IClock& clock,
                                           IMeshEventSink& events)
    : protocol_(protocol),
      identity_(identity),
      radio_(radio),
      clock_(clock),
      events_(events)
{
}

SendResult DirectMessageService::sendDirect(const DirectMessageCommand& command)
{
    if (!command.to.isValidUnicast() || command.payload.empty())
    {
        return SendResult::fail(SendFailure::InvalidInput);
    }

    LocalIdentity local{};
    auto local_result = identity_.ensureLocalIdentity(local);
    if (!local_result.ok || !local.valid)
    {
        events_.emit(MeshEvent{MeshEventKind::SendFailed,
                               command.to,
                               static_cast<int>(SendFailure::LocalIdentityMissing)});
        return SendResult::fail(SendFailure::LocalIdentityMissing);
    }

    PeerPublicKey peer{};
    auto peer_result = identity_.findPeerKey(command.to, peer);
    if (!peer_result.ok)
    {
        events_.emit(MeshEvent{MeshEventKind::PeerKeyMissing, command.to, 0});
        return SendResult::fail(SendFailure::PeerKeyMissing);
    }

    ProtocolBuildContext context{};
    context.local_node = command.from;
    context.local_identity = local;
    context.peer_key = peer;
    context.now_ms = clock_.nowMs();

    EncodedPacket packet{};
    auto built = protocol_.buildDirectMessage(context, command, packet);
    if (!built.ok)
    {
        events_.emit(MeshEvent{MeshEventKind::SendFailed,
                               command.to,
                               static_cast<int>(built.failure)});
        if (built.failure == ProtocolFailure::MissingPeerKey)
        {
            return SendResult::fail(SendFailure::PeerKeyMissing);
        }
        if (built.failure == ProtocolFailure::CryptoFailed)
        {
            return SendResult::fail(SendFailure::CryptoFailed);
        }
        if (built.failure == ProtocolFailure::Unsupported)
        {
            return SendResult::fail(SendFailure::ProtocolUnsupported);
        }
        return SendResult::fail(SendFailure::PacketBuildFailed);
    }

    auto sent = radio_.send(packet.view());
    if (!sent.ok)
    {
        events_.emit(MeshEvent{MeshEventKind::RadioError,
                               command.to,
                               static_cast<int>(sent.failure)});
        events_.emit(MeshEvent{MeshEventKind::SendFailed,
                               command.to,
                               static_cast<int>(SendFailure::RadioSendFailed)});
        return SendResult::fail(SendFailure::RadioSendFailed);
    }

    events_.emit(MeshEvent{MeshEventKind::MessageSent, command.to, 0});
    return SendResult::success();
}

} // namespace mesh
