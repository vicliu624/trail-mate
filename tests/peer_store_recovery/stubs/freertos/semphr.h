#pragma once
#include "FreeRTOS.h"
#include <cassert>
struct TestSemaphore
{
    unsigned depth = 0;
    bool recursive = false;
};
using SemaphoreHandle_t = TestSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new TestSemaphore; }
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex()
{
    auto* value = new TestSemaphore;
    value->recursive = true;
    return value;
}
inline int xSemaphoreTake(SemaphoreHandle_t value, TickType_t)
{
    if (!value || (value->depth && !value->recursive)) return pdFALSE;
    ++value->depth;
    return pdTRUE;
}
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t value, TickType_t ticks) { return xSemaphoreTake(value, ticks); }
inline int xSemaphoreGive(SemaphoreHandle_t value)
{
    assert(value && value->depth);
    --value->depth;
    return pdTRUE;
}
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t value) { return xSemaphoreGive(value); }
inline void vSemaphoreDelete(SemaphoreHandle_t value)
{
    assert(!value || !value->depth);
    delete value;
}
