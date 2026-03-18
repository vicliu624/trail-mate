#include "apps/gat562_mesh_evb_pro/debug_console.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace apps::gat562_mesh_evb_pro::debug_console
{
namespace
{

constexpr unsigned long kBaudRate = 115200UL;

void writeToUart(const char* text)
{
    if (!text)
    {
        return;
    }
    Serial2.print(text);
}

} // namespace

void begin()
{
    Serial2.begin(kBaudRate);
    delay(40);
}

void print(const char* text)
{
    writeToUart(text);
}

void println()
{
    Serial2.println();
}

void println(const char* text)
{
    writeToUart(text);
    Serial2.println();
}

void printf(const char* format, ...)
{
    if (!format)
    {
        return;
    }

    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print(buffer);
}

} // namespace apps::gat562_mesh_evb_pro::debug_console
