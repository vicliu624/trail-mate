#pragma once
#include <cstdint>
#include <cstdio>
struct TestSerial
{
    template <typename... Args>
    int printf(const char* format, Args... args) { return std::printf(format, args...); }
};
inline TestSerial Serial;
inline uint32_t millis() { return 100000; }
