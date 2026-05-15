#pragma once

#include <stddef.h>

namespace apps::gat562_mesh_evb_pro::debug_console
{

void begin();
void print(const char* text);
void println();
void println(const char* text);
void printf(const char* format, ...);

} // namespace apps::gat562_mesh_evb_pro::debug_console
