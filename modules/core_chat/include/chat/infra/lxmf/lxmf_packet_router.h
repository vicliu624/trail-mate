/**
 * @file lxmf_packet_router.h
 * @brief Single routing decision point for Reticulum packets entering LXMF.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/lxmf/lxmf_runtime_state.h"

namespace chat::lxmf::runtime
{

enum class PacketRoute
{
    Announce,
    Proof,
    LinkRequest,
    Data,
    LinkOrTransport,
};

enum class PacketForwardHeader : uint8_t
{
    None = 0,
    Header1Broadcast = 1,
    Header2Transport = 2,
};

struct PacketForwardPlan
{
    bool forward = false;
    PacketForwardHeader header = PacketForwardHeader::None;
    uint8_t interface_id = 0;
    uint8_t next_hop_transport[reticulum::kTruncatedHashSize] = {};
    uint8_t hops = 0;
};

class ReticulumPacketRouter
{
  public:
    PacketRoute route(const reticulum::ParsedPacket& packet) const;
    PacketForwardPlan planPathForward(const PathEntry& path,
                                      uint8_t packet_hops) const;
    PacketForwardPlan planLinkRelayForward(
        const LinkRelayEntry& relay,
        uint8_t ingress_interface_id,
        uint8_t packet_hops) const;
};

} // namespace chat::lxmf::runtime
