#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr int pdTRUE = 1, pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(value) (value)
