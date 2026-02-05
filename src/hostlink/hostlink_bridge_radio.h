#pragma once

#include "../sys/event_bus.h"

namespace hostlink::bridge
{

void on_event(const sys::Event& event);
void on_link_ready();

} // namespace hostlink::bridge
