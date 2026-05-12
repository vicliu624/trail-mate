#pragma once

#include "mesh/domain/mesh_packet.h"
#include "mesh/domain/radio_packet.h"

namespace mesh
{

class IPacketRadio
{
  public:
    virtual ~IPacketRadio() = default;

    virtual RadioResult configure(const RadioConfig& config) = 0;
    virtual RadioResult send(ByteView packet) = 0;
    virtual bool poll(RadioRxPacket& out) = 0;
};

} // namespace mesh
