#include "apps/gat562_mesh_evb_pro/debug_console.h"

#include <Arduino.h>
#include <cstring>
#include <stdarg.h>
#include <stdio.h>

namespace apps::gat562_mesh_evb_pro::debug_console
{
namespace
{

constexpr unsigned long kBaudRate = 115200UL;

bool usbSerialWritable(std::size_t len)
{
    return static_cast<bool>(Serial) && Serial.dtr() != 0 && Serial.availableForWrite() >= static_cast<int>(len);
}

void writeToUsbSerial(const char* text)
{
    if (!text)
    {
        return;
    }
    const std::size_t len = std::strlen(text);
    if (!usbSerialWritable(len))
    {
        return;
    }
    Serial.print(text);
}

void writeToJlinkSerial(const char* text)
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
    Serial.begin(kBaudRate);
    Serial2.begin(kBaudRate);
    delay(80);
}

void print(const char* text)
{
    writeToUsbSerial(text);
    writeToJlinkSerial(text);
}

void println()
{
    Serial.println();
    Serial2.println();
}

void println(const char* text)
{
    writeToUsbSerial(text);
    writeToJlinkSerial(text);
    println();
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
