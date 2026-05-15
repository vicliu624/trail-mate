#pragma once

namespace apps::gat562_mesh_evb_pro::app_runtime_access
{

struct Status
{
    bool initialized = false;
    bool app_facade_bound = false;
};

bool initialize();
void tick();
const Status& status();

} // namespace apps::gat562_mesh_evb_pro::app_runtime_access
