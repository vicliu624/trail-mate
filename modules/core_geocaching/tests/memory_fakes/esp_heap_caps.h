#pragma once
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <map>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
inline bool fail_psram = false;
inline unsigned internal_allocations = 0, preferred_allocations = 0;
inline std::map<void*, unsigned> memory_domains;
inline void* heap_caps_malloc(size_t size, unsigned caps)
{
    if (caps & MALLOC_CAP_INTERNAL) ++internal_allocations;
    if ((caps & MALLOC_CAP_SPIRAM) && fail_psram) return nullptr;
    void* pointer = std::malloc(size);
    if (pointer) memory_domains[pointer] = caps;
    return pointer;
}
inline void heap_caps_free(void* pointer)
{
    if (!pointer) return;
    assert(memory_domains.erase(pointer) == 1);
    std::free(pointer);
}
inline bool esp_ptr_external_ram(const void* pointer)
{
    const auto found = memory_domains.find(const_cast<void*>(pointer));
    return found != memory_domains.end() && (found->second & MALLOC_CAP_SPIRAM);
}
inline void* heap_caps_realloc(void* pointer, size_t size, unsigned caps)
{
    if ((caps & MALLOC_CAP_SPIRAM) && fail_psram) return nullptr;
    if (caps & MALLOC_CAP_INTERNAL) ++internal_allocations;
    auto node = memory_domains.extract(pointer);
    void* result = std::realloc(pointer, size);
    if (result) memory_domains[result] = caps;
    else if (!node.empty()) memory_domains.insert(std::move(node));
    return result;
}
inline void* heap_caps_malloc_prefer(size_t size, unsigned, unsigned first, unsigned second)
{
    ++preferred_allocations;
    auto* pointer = heap_caps_malloc(size, first);
    return pointer ? pointer : heap_caps_malloc(size, second);
}
inline void* heap_caps_realloc_prefer(void* pointer, size_t size, unsigned, unsigned first, unsigned second)
{
    ++preferred_allocations;
    auto* result = heap_caps_realloc(pointer, size, first);
    return result ? result : heap_caps_realloc(pointer, size, second);
}
