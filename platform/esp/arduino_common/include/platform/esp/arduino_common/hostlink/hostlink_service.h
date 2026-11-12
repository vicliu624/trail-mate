#pragma once

#include "hostlink/hostlink_session.h"

#include <cstddef>
#include <cstdint>

namespace hostlink
{

void start();
void stop();
bool is_active();
Status get_status();
bool enqueue_event(uint8_t type, const uint8_t* payload, size_t len, bool high_priority = false,
                   uint16_t reply_sequence = 0, uint32_t completion_cookie = 0);
void process_pending_commands();
void tick_publication_tasks();

} // namespace hostlink
