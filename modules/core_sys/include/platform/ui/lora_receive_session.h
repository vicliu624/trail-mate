#pragma once

#include "platform/ui/lora_runtime.h"

namespace platform::ui::lora
{
enum class ReceiveSessionState : uint8_t
{
    Idle,
    Configuring,
    Ready,
    Failed
};
struct ReceiveSessionSnapshot
{
    uint32_t generation = 0;
    ReceiveSessionState state = ReceiveSessionState::Idle;
    bool transmit_pending = false;
};

// Queue admission is distinct from configuration/TX completion. Consumers must
// match generation and wait for Ready / !transmit_pending before starting RF
// dwell/response timers. Releasing an obsolete generation is a no-op.
#if defined(TRAIL_MATE_PLATFORM_LINUX) || (defined(__linux__) && !defined(ESP_PLATFORM) && !defined(ARDUINO))
uint32_t request_receive_session(float frequency_mhz, const ReceiveConfig& config);
ReceiveSessionSnapshot receive_session_snapshot();
bool poll_receive_session(uint32_t generation, uint8_t* buffer, size_t capacity, ReceivedPacket* packet);
bool transmit_receive_session(uint32_t generation, const uint8_t* bytes, size_t size);
void release_receive_session(uint32_t generation);
#else
namespace detail
{
inline ReceiveSessionSnapshot& synchronous_receive_session()
{
    static ReceiveSessionSnapshot state;
    return state;
}
} // namespace detail
inline uint32_t request_receive_session(float frequency_mhz, const ReceiveConfig& config)
{
    auto& state = detail::synchronous_receive_session();
    if (++state.generation == 0) ++state.generation;
    state.transmit_pending = false;
    state.state = acquire() && configure_receive(frequency_mhz, config) ? ReceiveSessionState::Ready : ReceiveSessionState::Failed;
    return state.generation;
}
inline ReceiveSessionSnapshot receive_session_snapshot() { return detail::synchronous_receive_session(); }
inline bool poll_receive_session(uint32_t generation, uint8_t* buffer, size_t capacity, ReceivedPacket* packet)
{
    const auto state = receive_session_snapshot();
    return generation && state.generation == generation && state.state == ReceiveSessionState::Ready &&
           poll_received_packet(buffer, capacity, packet);
}
inline bool transmit_receive_session(uint32_t generation, const uint8_t* bytes, size_t size)
{
    auto& state = detail::synchronous_receive_session();
    if (!generation || state.generation != generation || state.state != ReceiveSessionState::Ready) return false;
    if (transmit_packet(bytes, size)) return true;
    state.state = ReceiveSessionState::Failed;
    return false;
}
inline void release_receive_session(uint32_t generation)
{
    auto& state = detail::synchronous_receive_session();
    if (!generation || state.generation != generation) return;
    release();
    state.state = ReceiveSessionState::Idle;
    state.transmit_pending = false;
}
#endif
} // namespace platform::ui::lora
