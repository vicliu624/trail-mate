#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

namespace ui::map_tiles
{
// Native readers, owned events and LVGL descriptors share these buffers.
// Generic PNG/POI worker scratch never carries a native ownership header.
// One allocation is shared by the event and LVGL descriptor; no pixel copy at
// descriptor creation. The budget counts queued AND displayed allocations.
class alignas(16) NativePixelBuffer
{
  public:
    using Allocate = void* (*)(size_t alignment, size_t bytes);
    using Free = void (*)(void*);
    static uint8_t* create(size_t bytes, std::atomic<size_t>& used, size_t limit, Allocate allocate, Free free)
    {
        if (bytes != 128U * 1024U && bytes != 256U * 1024U) return nullptr;
        const size_t total = sizeof(NativePixelBuffer) + bytes;
        auto current = used.load(std::memory_order_relaxed);
        do
        {
            if (current > limit || total > limit - current) return nullptr;
        } while (!used.compare_exchange_weak(current, current + total, std::memory_order_relaxed));
        auto* memory = allocate(alignof(NativePixelBuffer), total);
        if (!memory)
        {
            used.fetch_sub(total, std::memory_order_relaxed);
            return nullptr;
        }
        auto* buffer = new (memory) NativePixelBuffer(used, total, free);
        return reinterpret_cast<uint8_t*>(buffer + 1);
    }
    static void retain(const uint8_t* pixels)
    {
        owner(pixels)->references_.fetch_add(1, std::memory_order_relaxed);
    }
    static void release(const uint8_t* pixels)
    {
        auto* buffer = owner(pixels);
        if (buffer->references_.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
        auto* used = buffer->used_;
        const auto total = buffer->total_;
        const auto free = buffer->free_;
        buffer->~NativePixelBuffer();
        free(buffer);
        used->fetch_sub(total, std::memory_order_relaxed);
    }
    // Single LVGL consumer, after CRC validation. Conversion is once per lease.
    static void convertRgbaToBgra(const uint8_t* pixels, size_t bytes)
    {
        auto* buffer = owner(pixels);
        if (buffer->converted_) return;
        auto* out = const_cast<uint8_t*>(pixels);
        for (size_t i = 0; i < bytes; i += 4)
        {
            const auto red = out[i];
            out[i] = out[i + 2];
            out[i + 2] = red;
        }
        buffer->converted_ = true;
    }

  private:
    NativePixelBuffer(std::atomic<size_t>& used, size_t total, Free free) : used_(&used), total_(total), free_(free) {}
    static NativePixelBuffer* owner(const uint8_t* pixels) { return reinterpret_cast<NativePixelBuffer*>(const_cast<uint8_t*>(pixels)) - 1; }
    std::atomic<uint32_t> references_{1};
    std::atomic<size_t>* used_;
    size_t total_;
    Free free_;
    bool converted_ = false;
};
} // namespace ui::map_tiles
