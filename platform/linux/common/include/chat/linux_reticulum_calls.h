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
