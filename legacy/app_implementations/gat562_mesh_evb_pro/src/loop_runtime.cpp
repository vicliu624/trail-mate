#include "apps/gat562_mesh_evb_pro/loop_runtime.h"

#include <Arduino.h>

#include "apps/gat562_mesh_evb_pro/app_runtime_access.h"

namespace apps::gat562_mesh_evb_pro::loop_runtime
{

void tick()
{
    app_runtime_access::tick();
    delay(2);
}

} // namespace apps::gat562_mesh_evb_pro::loop_runtime
