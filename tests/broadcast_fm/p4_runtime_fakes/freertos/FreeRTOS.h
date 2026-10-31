#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
using TickType_t = uint32_t;
using TaskHandle_t = void*;
struct StaticSemaphore_t
{
    std::mutex mutex;
    std::condition_variable wake;
    bool token = false;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
constexpr int pdPASS = 1, pdTRUE = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) ((ms) / 10)
