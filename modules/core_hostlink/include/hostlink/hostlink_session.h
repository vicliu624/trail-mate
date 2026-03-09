#pragma once

#include <cstdint>

namespace hostlink
{

enum class LinkState : uint8_t
{
    Stopped = 0,
    Waiting = 1,
    Connected = 2,
    Handshaking = 3,
    Ready = 4,
    Error = 5,
};

struct Status
{
    LinkState state = LinkState::Stopped;
    uint32_t rx_count = 0;
    uint32_t tx_count = 0;
    uint32_t last_error = 0;
};

struct SessionRuntime
{
    Status status{};
    uint16_t tx_seq = 1;
    uint32_t handshake_deadline_ms = 0;
    uint32_t last_status_ms = 0;
    uint32_t last_gps_ms = 0;
};

void reset_session(SessionRuntime& runtime, uint32_t now_ms);
void stop_session(SessionRuntime& runtime);
void set_link_state(SessionRuntime& runtime, LinkState state);

bool is_ready(const SessionRuntime& runtime);
bool is_waiting(const SessionRuntime& runtime);
bool is_handshaking(const SessionRuntime& runtime);

void note_rx(SessionRuntime& runtime);
void note_tx(SessionRuntime& runtime);
void note_error(SessionRuntime& runtime, uint32_t error_code);
uint16_t next_tx_sequence(SessionRuntime& runtime);

void mark_handshake_started(SessionRuntime& runtime, uint32_t now_ms, uint32_t timeout_ms);
void mark_handshake_complete(SessionRuntime& runtime, uint32_t now_ms);
void mark_disconnected(SessionRuntime& runtime);
bool handshake_expired(SessionRuntime& runtime, uint32_t now_ms);

bool should_emit_status(const SessionRuntime& runtime, uint32_t now_ms, uint32_t interval_ms);
void mark_status_emitted(SessionRuntime& runtime, uint32_t now_ms);
bool should_emit_gps(const SessionRuntime& runtime, uint32_t now_ms, uint32_t interval_ms);
void mark_gps_emitted(SessionRuntime& runtime, uint32_t now_ms);

} // namespace hostlink
