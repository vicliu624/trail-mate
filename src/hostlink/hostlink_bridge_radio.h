#pragma once

#include "../sys/event_bus.h"

namespace hostlink::bridge
{

struct AppRxStats
{
    uint32_t total = 0;
    uint32_t from_is = 0;
    uint32_t direct = 0;
    uint32_t relayed = 0;
};

void on_event(const sys::Event& event);
void on_link_ready();
AppRxStats get_app_rx_stats();

} // namespace hostlink::bridge
