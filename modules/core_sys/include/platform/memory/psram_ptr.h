#pragma once

#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace platform::memory
{
// Owned application state must not silently consume the radio/RTOS heap.
// Host builds keep nothrow-new fault injection and matching deallocation.
inline void* allocatePsram(std::size_t size)
{
#if defined(ESP_PLATFORM)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return ::operator new(size, std::nothrow);
#endif
}
inline void freePsram(void* pointer)
{
#if defined(ESP_PLATFORM)
    heap_caps_free(pointer);
#else
    ::operator delete(pointer);
#endif
}
template <class T, class... Args>
T* createPsram(Args&&... args)
{
    void* memory = allocatePsram(sizeof(T));
    if (!memory) return nullptr;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    try
    {
        return ::new (memory) T(std::forward<Args>(args)...);
    }
    catch (...)
    {
        freePsram(memory);
        throw;
    }
#else
    return ::new (memory) T(std::forward<Args>(args)...);
#endif
}
template <class T>
void destroyPsram(T* pointer)
{
    if (!pointer) return;
    pointer->~T();
    freePsram(pointer);
}
template <class T>
struct PsramDeleter
{
    void operator()(T* pointer) const { destroyPsram(pointer); }
};
template <class T>
using PsramPtr = std::unique_ptr<T, PsramDeleter<T>>;
} // namespace platform::memory
