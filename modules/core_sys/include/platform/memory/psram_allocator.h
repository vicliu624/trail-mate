#pragma once

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace platform::memory
{

[[noreturn]] inline void psramAllocationFailed()
{
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    throw std::bad_alloc();
#else
    std::abort();
#endif
}

template <typename T>
class PsramAllocator
{
  public:
    using value_type = T;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    PsramAllocator() noexcept = default;

    template <typename U>
    PsramAllocator(const PsramAllocator<U>&) noexcept
    {
    }

    T* allocate(std::size_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
        {
            psramAllocationFailed();
        }
        const std::size_t bytes = count * sizeof(T);
        if (bytes == 0)
        {
            return nullptr;
        }
#if defined(ESP_PLATFORM)
        void* ptr = heap_caps_malloc(bytes,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void* ptr = std::malloc(bytes);
#endif
        if (!ptr)
        {
            psramAllocationFailed();
        }
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept
    {
        if (!ptr)
        {
            return;
        }
#if defined(ESP_PLATFORM)
        heap_caps_free(ptr);
#else
        std::free(ptr);
#endif
    }

    template <typename U>
    struct rebind
    {
        using other = PsramAllocator<U>;
    };
};

template <typename T, typename U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept
{
    return true;
}

template <typename T, typename U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept
{
    return false;
}

} // namespace platform::memory
