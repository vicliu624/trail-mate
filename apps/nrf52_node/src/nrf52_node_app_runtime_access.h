#pragma once

namespace trailmate::apps::nrf52_node::app_runtime_access
{

struct Status
{
    bool initialized = false;
    bool app_facade_bound = false;
};

bool initialize();
void tick();
const Status& status();

} // namespace trailmate::apps::nrf52_node::app_runtime_access