#include "apps/gat562_mesh_evb_pro/arduino_entry.h"

#include "apps/gat562_mesh_evb_pro/loop_runtime.h"
#include "apps/gat562_mesh_evb_pro/startup_runtime.h"

namespace apps::gat562_mesh_evb_pro::arduino_entry
{

void setup()
{
    startup_runtime::run();
}

void loop()
{
    loop_runtime::tick();
}

} // namespace apps::gat562_mesh_evb_pro::arduino_entry
