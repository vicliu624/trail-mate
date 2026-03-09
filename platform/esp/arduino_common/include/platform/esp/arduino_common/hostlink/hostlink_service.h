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
bool enqueue_event(uint8_t type, const uint8_t* payload, size_t len, bool high_priority = false);
void process_pending_commands();

} // namespace hostlink
