#pragma once
#include <cstdint>
using TickType_t = uint32_t;
struct StaticSemaphore_t
{
    bool token = false;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((ms) / 10)
