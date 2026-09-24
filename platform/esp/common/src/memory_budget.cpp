#include "platform/esp/common/memory_budget.h"

#include <cstdio>
#include <limits>

#include <esp_heap_caps.h>
#include <soc/soc_memory_types.h>

namespace platform::esp::common::memory
{
namespace
{

std::size_t safe_required_plus_floor(std::size_t required, std::size_t floor)
{
    if (required > std::numeric_limits<std::size_t>::max() - floor)
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return required + floor;
}

} // namespace

HeapSnapshot capture()
{
    HeapSnapshot snapshot{};
    snapshot.internal_free =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.internal_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    snapshot.dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    snapshot.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    snapshot.psram_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    snapshot.minimum_internal_free =
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.minimum_dma_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    snapshot.minimum_psram_free =
        heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return snapshot;
}

bool meetsFloor(const HeapSnapshot& snapshot,
                std::size_t required_internal,
                std::size_t required_dma,
                std::size_t required_psram,
                std::size_t internal_floor,
                std::size_t dma_floor,
                std::size_t psram_floor)
{
    const std::size_t internal_needed =
        safe_required_plus_floor(required_internal, internal_floor);
    const std::size_t dma_needed = safe_required_plus_floor(required_dma, dma_floor);
    const std::size_t psram_needed =
        safe_required_plus_floor(required_psram, psram_floor);

    return snapshot.internal_free >= internal_needed &&
           snapshot.internal_largest >= required_internal &&
           snapshot.dma_free >= dma_needed &&
           snapshot.dma_largest >= required_dma &&
           snapshot.psram_free >= psram_needed &&
           snapshot.psram_largest >= required_psram;
}

bool admit(const char* owner,
           std::size_t required_internal,
           std::size_t required_dma,
           std::size_t required_psram,
           std::size_t internal_floor,
           std::size_t dma_floor,
           std::size_t psram_floor)
{
    const HeapSnapshot snapshot = capture();
    const bool accepted = meetsFloor(snapshot,
                                     required_internal,
                                     required_dma,
                                     required_psram,
                                     internal_floor,
                                     dma_floor,
                                     psram_floor);
    if (!accepted)
    {
        std::printf("[Mem][Admission] owner=%s accepted=0 "
                    "required_internal=%u required_dma=%u required_psram=%u "
                    "floor_internal=%u floor_dma=%u floor_psram=%u "
                    "internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u "
                    "psram_free=%u psram_largest=%u\n",
                    owner ? owner : "<unnamed>",
                    static_cast<unsigned>(required_internal),
                    static_cast<unsigned>(required_dma),
                    static_cast<unsigned>(required_psram),
                    static_cast<unsigned>(internal_floor),
                    static_cast<unsigned>(dma_floor),
                    static_cast<unsigned>(psram_floor),
                    static_cast<unsigned>(snapshot.internal_free),
                    static_cast<unsigned>(snapshot.internal_largest),
                    static_cast<unsigned>(snapshot.dma_free),
                    static_cast<unsigned>(snapshot.dma_largest),
                    static_cast<unsigned>(snapshot.psram_free),
                    static_cast<unsigned>(snapshot.psram_largest));
    }
    return accepted;
}

void* allocatePreferred(const char* owner, std::size_t bytes, bool allow_internal_fallback)
{
    if (bytes == 0U)
    {
        return nullptr;
    }

    void* storage = nullptr;
    const bool has_psram =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != 0U;
    if (has_psram)
    {
        storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    else if (allow_internal_fallback)
    {
        storage = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    if (storage == nullptr)
    {
        std::printf("[Mem][Budget] allocation failed owner=%s bytes=%u "
                    "psram_present=%u internal_fallback=%u\n",
                    owner ? owner : "<unnamed>",
                    static_cast<unsigned>(bytes),
                    has_psram ? 1U : 0U,
                    allow_internal_fallback ? 1U : 0U);
        return nullptr;
    }

#if defined(TRAIL_MATE_VERBOSE_RUNTIME_LOGS) && TRAIL_MATE_VERBOSE_RUNTIME_LOGS
    std::printf("[Mem][Budget] allocation owner=%s bytes=%u domain=%s\n",
                owner ? owner : "<unnamed>",
                static_cast<unsigned>(bytes),
                esp_ptr_external_ram(storage) ? "psram" : "internal");
#endif
    return storage;
}

void logSnapshot(const char* owner, const char* stage)
{
    const HeapSnapshot snapshot = capture();
    std::printf("[Mem][Budget] owner=%s stage=%s "
                "internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u "
                "psram_free=%u psram_largest=%u min_internal=%u min_dma=%u min_psram=%u\n",
                owner ? owner : "<unnamed>",
                stage ? stage : "state",
                static_cast<unsigned>(snapshot.internal_free),
                static_cast<unsigned>(snapshot.internal_largest),
                static_cast<unsigned>(snapshot.dma_free),
                static_cast<unsigned>(snapshot.dma_largest),
                static_cast<unsigned>(snapshot.psram_free),
                static_cast<unsigned>(snapshot.psram_largest),
                static_cast<unsigned>(snapshot.minimum_internal_free),
                static_cast<unsigned>(snapshot.minimum_dma_free),
                static_cast<unsigned>(snapshot.minimum_psram_free));
}

} // namespace platform::esp::common::memory
