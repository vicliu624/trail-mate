/**
 * @file lxmf_packet_router.cpp
 * @brief Single routing decision point for Reticulum packets entering LXMF.
 */

#include "chat/infra/lxmf/lxmf_packet_router.h"

#include <cstring>

namespace chat::lxmf::runtime
{

PacketRoute ReticulumPacketRouter::route(const reticulum::ParsedPacket& packet) const
{
    switch (packet.packet_type)
    {
    case reticulum::PacketType::Announce:
        return PacketRoute::Announce;
    case reticulum::PacketType::Proof:
        return PacketRoute::Proof;
    case reticulum::PacketType::LinkRequest:
        return PacketRoute::LinkRequest;
    case reticulum::PacketType::Data:
        return PacketRoute::Data;
    default:
        return PacketRoute::LinkOrTransport;
    }
}

PacketForwardPlan ReticulumPacketRouter::planPathForward(
    const PathEntry& path,
    uint8_t packet_hops) const
{
    PacketForwardPlan plan{};
    plan.forward = true;
    plan.interface_id = path.interface_id;
    plan.hops = packet_hops;
    if (path.hops <= 1 || path.direct)
    {
        plan.header = PacketForwardHeader::Header1Broadcast;
        return plan;
    }

    plan.header = PacketForwardHeader::Header2Transport;
    std::memcpy(plan.next_hop_transport,
                path.next_hop_transport,
                sizeof(plan.next_hop_transport));
    return plan;
}

PacketForwardPlan ReticulumPacketRouter::planLinkRelayForward(
    const LinkRelayEntry& relay,
    uint8_t ingress_interface_id,
    uint8_t packet_hops) const
{
    const bool from_initiator =
        packet_hops == relay.initiator_hops &&
        (ingress_interface_id == 0 ||
         ingress_interface_id == relay.initiator_interface_id);
    const bool from_responder =
        packet_hops == relay.responder_hops &&
        (ingress_interface_id == 0 ||
         ingress_interface_id == relay.responder_interface_id);
    if (!from_initiator && !from_responder)
    {
        return {};
    }

    PacketForwardPlan plan{};
    plan.forward = true;
    plan.header = PacketForwardHeader::Header1Broadcast;
    plan.interface_id = from_initiator ? relay.responder_interface_id
                                       : relay.initiator_interface_id;
    plan.hops = packet_hops;
    return plan;
}

} // namespace chat::lxmf::runtime
