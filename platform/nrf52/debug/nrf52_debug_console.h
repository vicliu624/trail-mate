#pragma once

#include <stddef.h>

namespace platform::nrf52::debug_console
{

void begin();
void print(const char* text);
void println();
void println(const char* text);
void printf(const char* format, ...);

} // namespace platform::nrf52::debug_console
