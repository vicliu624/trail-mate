#pragma once
#include <cstdlib>
#include <cstddef>
inline constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline unsigned allocation_caps = 0, allocations = 0, deallocations = 0;
inline bool fail_allocation = false;
inline void* heap_caps_malloc(size_t size, unsigned caps)
{
    allocation_caps = caps;
    ++allocations;
    return fail_allocation ? nullptr : std::malloc(size);
}
inline void heap_caps_free(void* memory) { ++deallocations; std::free(memory); }
