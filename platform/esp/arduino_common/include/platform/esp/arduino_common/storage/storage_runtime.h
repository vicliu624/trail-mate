#pragma once

#include "chat/domain/chat_types.h"

namespace chat
{
class SdStore;
class SdProtocolPeerRepository;
} // namespace chat

namespace platform::esp::arduino_common::storage
{

// Reserves the existing worker's queue and stack before optional startup
// allocations. Does not arm recovery or perform SD I/O.
bool prepare_deferred_storage();

// Arms the stable storage maintenance owner. The task remains blocked on its
// event queue after maintenance completes.
void start_deferred_storage(chat::SdStore* store,
                            chat::SdProtocolPeerRepository* peer_directory,
                            chat::MeshProtocol active_protocol);

// Advances the retry/maintenance state machine from the foreground loop.
void tick_deferred_storage();

// Requests cancellation at the next operation boundary and stops accepting
// foreground maintenance ticks.
void stop_deferred_storage();

// Returns true while the initial SD-backed state is being hydrated. The
// foreground lifecycle must not enter store/repository code during this
// exclusive hydration phase.
bool hydration_active();

// Returns true from the initial arm through successful initial hydration,
// including the display gate and retry backoff. User-facing pages use this to
// distinguish a loading cache from a genuinely empty store.
bool initial_hydration_pending();

// Returns true after the initial hydration succeeded and remains true until a
// new storage-owner lifecycle begins. This does not consume the one-shot
// hydration-ready notification.
bool initial_hydration_ready();

// Returns true once after the initial hydration has completed successfully.
// The foreground loop uses this edge to apply non-critical SD-backed state.
bool consume_hydration_ready();

} // namespace platform::esp::arduino_common::storage
