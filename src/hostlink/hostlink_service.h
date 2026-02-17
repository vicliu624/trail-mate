#pragma once

#include "hostlink_types.h"
#include <cstddef>
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

void start();
void stop();
bool is_active();
Status get_status();
bool enqueue_event(uint8_t type, const uint8_t* payload, size_t len, bool high_priority = false);
void process_pending_commands();

} // namespace hostlink
