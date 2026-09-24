#pragma once
#include "FreeRTOS.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using SemaphoreHandle_t = HANDLE;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return CreateSemaphoreW(nullptr, 1, 1, nullptr); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return CreateSemaphoreW(nullptr, 0, 1, nullptr); }
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { CloseHandle(semaphore); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    return WaitForSingleObject(semaphore, timeout == portMAX_DELAY ? INFINITE : timeout) == WAIT_OBJECT_0 ? pdTRUE : pdFALSE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    return ReleaseSemaphore(semaphore, 1, nullptr) ? pdTRUE : pdFALSE;
}
#else
#include <chrono>
#include <condition_variable>
#include <mutex>

struct TestSemaphore
{
    std::mutex mutex;
    std::condition_variable changed;
    bool available = false;
};
using SemaphoreHandle_t = TestSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    auto* value = new TestSemaphore;
    value->available = true;
    return value;
}
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new TestSemaphore; }
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    if (timeout == portMAX_DELAY)
        semaphore->changed.wait(lock, [&]
                                { return semaphore->available; });
    else if (!semaphore->changed.wait_for(lock, std::chrono::milliseconds(timeout), [&]
                                          { return semaphore->available; }))
        return pdFALSE;
    semaphore->available = false;
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    {
        std::lock_guard<std::mutex> lock(semaphore->mutex);
        semaphore->available = true;
    }
    semaphore->changed.notify_one();
    return pdTRUE;
}
#endif
