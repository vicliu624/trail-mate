#pragma once
#include <cstdint>
struct FakeFat
{
    bool begin(bool format, const char* path, uint8_t files, const char* label);
};
extern FakeFat FFat;
void yield();
