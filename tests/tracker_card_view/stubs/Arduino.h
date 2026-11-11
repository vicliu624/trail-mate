#pragma once
#include <cstdint>
inline uint32_t arduino_test_millis = 123000;
inline uint32_t millis() { return arduino_test_millis; }
inline long random(long first, long) { return first; }
