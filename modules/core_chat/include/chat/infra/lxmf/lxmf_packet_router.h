/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_packet_router.h
 * @brief Single routing decision point for Reticulum packets entering LXMF.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_runtime_state.h"
#include "chat/infra/lxmf/lxmf_wire.h"

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
