#pragma once

#include "mesh/ports/i_clock.h"
#include "mesh/ports/i_mesh_event_sink.h"
#include "mesh/protocol/mesh_protocol_strategy.h"
#include "mesh/usecase/mesh_dedup_service.h"
#include "mesh/usecase/peer_identity_service.h"

namespace mesh
{

class ReceivePacketService
{
  public:
    ReceivePacketService(MeshProtocolStrategy& protocol,
                         PeerIdentityService& identity,
                         IMeshEventSink& events,
                         IClock& clock,
                         MeshDedupService* dedup = nullptr);

    void onRadioPacket(const RadioRxPacket& packet);

  private:
    void handleProtocolEvent(const MeshProtocolEvent& event);

    MeshProtocolStrategy& protocol_;
    PeerIdentityService& identity_;
    IMeshEventSink& events_;
    IClock& clock_;
    MeshDedupService* dedup_ = nullptr;
};

} // namespace mesh
