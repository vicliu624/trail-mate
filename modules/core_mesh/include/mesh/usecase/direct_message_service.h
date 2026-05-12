#pragma once

#include "mesh/domain/direct_message.h"
#include "mesh/ports/i_clock.h"
#include "mesh/ports/i_mesh_event_sink.h"
#include "mesh/ports/i_packet_radio.h"
#include "mesh/protocol/mesh_protocol_strategy.h"
#include "mesh/usecase/peer_identity_service.h"

namespace mesh
{

class DirectMessageService
{
  public:
    DirectMessageService(MeshProtocolStrategy& protocol,
                         PeerIdentityService& identity,
                         IPacketRadio& radio,
                         IClock& clock,
                         IMeshEventSink& events);

    SendResult sendDirect(const DirectMessageCommand& command);

  private:
    MeshProtocolStrategy& protocol_;
    PeerIdentityService& identity_;
    IPacketRadio& radio_;
    IClock& clock_;
    IMeshEventSink& events_;
};

} // namespace mesh
