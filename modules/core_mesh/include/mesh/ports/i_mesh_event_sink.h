#pragma once

#include "mesh/domain/node_id.h"

#include <stdint.h>

namespace mesh
{

enum class MeshEventKind : uint8_t
{
    None = 0,
    MessageReceived,
    MessageSent,
    SendFailed,
    PeerDiscovered,
    PeerKeyLearned,
    PeerKeyMissing,
    RadioError,
    ProtocolError,
};

struct MeshEvent
{
    MeshEventKind kind = MeshEventKind::None;
    NodeId peer{};
    int code = 0;

    MeshEvent() = default;

    MeshEvent(MeshEventKind event_kind, NodeId event_peer, int event_code)
        : kind(event_kind),
          peer(event_peer),
          code(event_code)
    {
    }
};

class IMeshEventSink
{
  public:
    virtual ~IMeshEventSink() = default;

    virtual void emit(const MeshEvent& event) = 0;
};

} // namespace mesh
