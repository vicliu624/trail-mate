#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "chat/domain/chat_model.h"
#include "chat/ports/i_mesh_adapter.h"
#include "team/domain/team_types.h"

namespace trailmate::cardputer_zero::linux_ui
{

// ---------------------------------------------------------------------------
// DemoWorld
//
// Synthetic demo data for the simulator.  Seeds the contact + node stores
// with fake peers, provides a loopback mesh adapter, auto-reply logic, and
// dummy team crypto.
//
// This class exists SOLELY for development and simulator use.  It must not
// be active on a real device except with explicit opt-in (TRAIL_MATE_DEMO_WORLD=1).
// ---------------------------------------------------------------------------

struct DemoPeerSeed
{
    ::chat::NodeId node_id = 0;
    const char* short_name = nullptr;
    const char* long_name = nullptr;
    const char* nickname = nullptr;
    bool ignored = false;
    float snr = 0.0f;
    float rssi = 0.0f;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
};

class DemoWorld
{
  public:
    DemoWorld();
    ~DemoWorld();

    DemoWorld(const DemoWorld&) = delete;
    DemoWorld& operator=(const DemoWorld&) = delete;

    /// Returns the canonical set of synthetic peers.
    static std::array<DemoPeerSeed, 4> peerSeeds();

    /// Factory: create a loopback mesh adapter pre-configured with the
    /// given self node ID.  The adapter will auto-reply to messages and
    /// inject synthetic broadcast traffic.
    std::unique_ptr<::chat::IMeshAdapter>
    createLoopbackMesh(::chat::NodeId self_node_id);

    /// Compute an auto-reply text for a given peer.
    static std::string autoReplyText(::chat::NodeId peer,
                                     const std::string& received_text);

    /// Synthetic node IDs used by the demo world (constexpr for static_assert).
    static constexpr ::chat::NodeId alphaNodeId() { return 0x435A1001U; }
    static constexpr ::chat::NodeId bravoNodeId() { return 0x435A1002U; }
    static constexpr ::chat::NodeId scoutNodeId() { return 0x435A1003U; }
    static constexpr ::chat::NodeId nearbyNodeId() { return 0x435A1004U; }
    static constexpr ::chat::NodeId broadcastNodeId() { return alphaNodeId(); }

    /// Synthetic team identity used for demo pairing.
    static ::team::TeamId syntheticPairTeamId();
    static std::array<uint8_t, 32> syntheticPairPsk();
    static const char* syntheticPairTeamName();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::cardputer_zero::linux_ui
