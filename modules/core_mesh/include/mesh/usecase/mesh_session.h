#pragma once

#include "mesh/ports/i_clock.h"
#include "mesh/ports/i_packet_radio.h"
#include "mesh/protocol/mesh_protocol_strategy.h"
#include "mesh/usecase/direct_message_service.h"
#include "mesh/usecase/mesh_runtime_state.h"
#include "mesh/usecase/receive_packet_service.h"

namespace mesh
{

class MeshSession
{
  public:
    MeshSession(IPacketRadio& radio,
                MeshProtocolStrategy& protocol,
                DirectMessageService& direct,
                ReceivePacketService& receive,
                IClock& clock);

    bool start(const MeshRuntimeConfig& config);
    void stop();
    void tick();
    SendResult sendDirect(const DirectMessageCommand& command);
    MeshSessionState state() const;

  private:
    IPacketRadio& radio_;
    MeshProtocolStrategy& protocol_;
    DirectMessageService& direct_;
    ReceivePacketService& receive_;
    IClock& clock_;
    MeshSessionState state_ = MeshSessionState::Stopped;
};

} // namespace mesh
