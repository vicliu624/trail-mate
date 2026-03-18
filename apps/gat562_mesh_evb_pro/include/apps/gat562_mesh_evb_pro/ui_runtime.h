#pragma once

#include "boards/gat562_mesh_evb_pro/gat562_board.h"

namespace apps::gat562_mesh_evb_pro::ui_runtime
{

bool initialize();
void appendBootLog(const char* line);
void tick(const boards::gat562_mesh_evb_pro::BoardInputEvent* event);

} // namespace apps::gat562_mesh_evb_pro::ui_runtime
