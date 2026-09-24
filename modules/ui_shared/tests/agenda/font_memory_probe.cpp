#include "font_memory_probe.h"

#include <cassert>

// GNU-linker test instrumentation only. Firmware allocators are unchanged.
// Requested LVGL payload bytes exclude allocator headers/alignment and libc I/O.
namespace
{
struct Allocation
{
    void* pointer = nullptr;
    std::size_t bytes = 0;
};
Allocation allocations[512]{};
bool enabled = false;
agenda_test_fonts::FontMemory memory{};

void forget(void* pointer)
{
    if (!pointer) return;
    for (auto& slot : allocations)
        if (slot.pointer == pointer)
        {
            memory.bytes -= slot.bytes;
            --memory.allocations;
            slot = {};
            return;
        }
}
void remember(void* pointer, std::size_t bytes)
{
    if (!enabled || !pointer) return;
    for (auto& slot : allocations)
        if (!slot.pointer)
        {
            slot = {pointer, bytes};
            memory.bytes += bytes;
            ++memory.allocations;
            if (memory.bytes > memory.peak_bytes) memory.peak_bytes = memory.bytes;
            return;
        }
    assert(false && "Font probe allocation capacity exceeded");
}
} // namespace

extern "C"
{
    void* __real_lv_malloc_core(std::size_t);
    void* __real_lv_realloc_core(void*, std::size_t);
    void __real_lv_free_core(void*);

    void* __wrap_lv_malloc_core(std::size_t bytes)
    {
        void* result = __real_lv_malloc_core(bytes);
        remember(result, bytes);
        return result;
    }
    void* __wrap_lv_realloc_core(void* pointer, std::size_t bytes)
    {
        // Keep the old allocation registered on failure. Save its slot before
        // realloc, since a successful move invalidates the old pointer value.
        Allocation* previous = nullptr;
        for (auto& slot : allocations)
            if (pointer && slot.pointer == pointer) previous = &slot;
        void* result = __real_lv_realloc_core(pointer, bytes);
        if (result || bytes == 0)
        {
            if (previous)
            {
                memory.bytes -= previous->bytes;
                --memory.allocations;
                *previous = {};
            }
            remember(result, bytes);
        }
        return result;
    }
    void __wrap_lv_free_core(void* pointer)
    {
        forget(pointer);
        __real_lv_free_core(pointer);
    }
}

namespace agenda_test_fonts
{
void beginMemoryProbe()
{
    assert(!enabled && memory.allocations == 0);
    memory = {};
    enabled = true;
}
FontMemory memoryProbe() { return memory; }
void endMemoryProbe()
{
    assert(memory.bytes == 0 && memory.allocations == 0);
    enabled = false;
}
} // namespace agenda_test_fonts
