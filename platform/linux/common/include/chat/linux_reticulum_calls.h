/*
 * LXST-derived portions: https://github.com/markqvist/LXST
 * Copyright 2024-2026, Mark Qvist, unless otherwise noted.
 * Upstream reference terms: LICENSES/LXST-CC-BY-NC-ND-4.0.txt.
 * Sharing adapted LXST material requires separate authorization from Mark Qvist.
 * This notice does not grant that authorization.
 * Any RNS/LXMF-derived portions retain their original copyright and Reticulum
 * License; see LICENSES/Reticulum-Derivatives.txt, LICENSES/Reticulum-RNS.txt
 * and LICENSES/Reticulum-LXMF.txt. Other contributions retain their own terms.
 * Component scope: docs/licensing/reticulum-sources.csv; see LICENSE.
 */

#pragma once
#include "chat/linux_reticulum_links.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include <memory>

namespace trailmate::linux_app
{
// One protocol-worker owner. Owns call hooks, not the links or peer directory.
// Destroy before those dependencies. UI actions use the shared call runtime;
// this owner performs protocol/media setup work outside the UI thread.
class LinuxReticulumCalls
{
  public:
    explicit LinuxReticulumCalls(LinuxReticulumLinks&, ::chat::IMeshPeerDirectory&);
    ~LinuxReticulumCalls();
    LinuxReticulumCalls(const LinuxReticulumCalls&) = delete;
    LinuxReticulumCalls& operator=(const LinuxReticulumCalls&) = delete;
    struct Route
    {
        uint8_t hops = 1;
        const uint8_t* next_hop = nullptr;
        bool waiting_for_path = false;
    };
    ::chat::MeshActionResult dial(const ::chat::ReticulumPeerIdentity&, const Route&,
                                  uint32_t now, LinuxReticulumLinks::Sender, void*, uint8_t* prepared_link = nullptr);
    // ready is an actual telephony-capable transport fact, not generic IP or
    // radio presence. A caller must keep this false for unsupported transports.
    void tick(uint32_t now, bool ready, LinuxReticulumLinks::Sender, void*);
    void reset(uint32_t now, LinuxReticulumLinks::Sender, void*);
    struct Statistics
    {
        size_t sessions = 0;
        uint64_t dialled = 0, admitted = 0, rejected = 0, closed = 0;
        uint64_t received_audio = 0, sent_audio = 0, dropped = 0;
    };
    Statistics statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
