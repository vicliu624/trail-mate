#include "hostlink/hostlink_session.h"

namespace hostlink
{

void reset_session(SessionRuntime& runtime, uint32_t now_ms)
{
    runtime.status.state = LinkState::Waiting;
    runtime.status.rx_count = 0;
    runtime.status.tx_count = 0;
    runtime.status.last_error = 0;
    runtime.tx_seq = 1;
    runtime.handshake_deadline_ms = 0;
    runtime.last_status_ms = now_ms;
    runtime.last_gps_ms = now_ms;
}

void stop_session(SessionRuntime& runtime)
{
    runtime.status.state = LinkState::Stopped;
    runtime.handshake_deadline_ms = 0;
}

void set_link_state(SessionRuntime& runtime, LinkState state)
{
    runtime.status.state = state;
}

bool is_ready(const SessionRuntime& runtime)
{
    return runtime.status.state == LinkState::Ready;
}

bool is_waiting(const SessionRuntime& runtime)
{
    return runtime.status.state == LinkState::Waiting;
}

bool is_handshaking(const SessionRuntime& runtime)
{
    return runtime.status.state == LinkState::Handshaking;
}

void note_rx(SessionRuntime& runtime)
{
    runtime.status.rx_count++;
}

void note_tx(SessionRuntime& runtime)
{
    runtime.status.tx_count++;
}

void note_error(SessionRuntime& runtime, uint32_t error_code)
{
    runtime.status.last_error = error_code;
}

uint16_t next_tx_sequence(SessionRuntime& runtime)
{
    if (++runtime.tx_seq == 0)
    {
        runtime.tx_seq = 1;
    }
    return runtime.tx_seq;
}

void mark_handshake_started(SessionRuntime& runtime, uint32_t now_ms, uint32_t timeout_ms)
{
    runtime.status.state = LinkState::Handshaking;
    runtime.handshake_deadline_ms = now_ms + timeout_ms;
}

void mark_handshake_complete(SessionRuntime& runtime, uint32_t now_ms)
{
    runtime.status.state = LinkState::Ready;
    runtime.handshake_deadline_ms = 0;
    runtime.last_status_ms = now_ms;
    runtime.last_gps_ms = now_ms;
}

void mark_disconnected(SessionRuntime& runtime)
{
    runtime.status.state = LinkState::Waiting;
    runtime.handshake_deadline_ms = 0;
}

bool handshake_expired(SessionRuntime& runtime, uint32_t now_ms)
{
    if (!is_handshaking(runtime) || runtime.handshake_deadline_ms == 0)
    {
        return false;
    }
    if (now_ms <= runtime.handshake_deadline_ms)
    {
        return false;
    }
    runtime.status.state = LinkState::Waiting;
    runtime.handshake_deadline_ms = 0;
    return true;
}

bool should_emit_status(const SessionRuntime& runtime, uint32_t now_ms, uint32_t interval_ms)
{
    return is_ready(runtime) && (now_ms - runtime.last_status_ms >= interval_ms);
}

void mark_status_emitted(SessionRuntime& runtime, uint32_t now_ms)
{
    runtime.last_status_ms = now_ms;
}

bool should_emit_gps(const SessionRuntime& runtime, uint32_t now_ms, uint32_t interval_ms)
{
    return is_ready(runtime) && (now_ms - runtime.last_gps_ms >= interval_ms);
}

void mark_gps_emitted(SessionRuntime& runtime, uint32_t now_ms)
{
    runtime.last_gps_ms = now_ms;
}

} // namespace hostlink
