#include "platform/esp/arduino_common/memory/psram_allocator.h"
#include "platform/memory/psram_allocator.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
int main()
{
    using Allocator = platform::memory::PsramAllocator<uint8_t>;
    static_assert(std::is_same_v<Allocator, platform::esp::arduino_common::memory::PsramAllocator<uint8_t>>);
    {
        std::vector<uint8_t, Allocator> bytes(64);
        assert(bytes.size() == 64 && allocations == 1 && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    assert(deallocations == 1);
    fail_allocation = true;
    bool refused = false;
    try
    {
        std::vector<uint8_t, Allocator> bytes(64);
    }
    catch (const std::bad_alloc&)
    {
        refused = true;
    }
    assert(refused && deallocations == 1); // No fallback to the internal heap.
    platform::memory::PsramAllocator<uint64_t> wide;
    refused = false;
    const auto before = allocations;
    try
    {
        (void)wide.allocate(std::numeric_limits<size_t>::max());
    }
    catch (const std::bad_alloc&)
    {
        refused = true;
    }
    assert(refused && allocations == before);
}
