#pragma once

#include "platform/shared/radio/fm_iq_consumer.h"
#include <atomic>
#include <cstring>

namespace platform::shared::radio
{
// One capture producer and one DSP consumer. Persistent storage only: the
// bounded payload storage is deliberately unsuitable for an ESP task stack.
// Never reset a live queue. Stop and join both owners before destroying it.
class FmIqQueue
{
  public:
    static constexpr uint32_t kDepth = 16;
    struct Block
    {
        uint64_t session = 0;
        uint64_t first_pair = 0;
        uint32_t generation = 0;
        uint32_t rate = 0;
        size_t bytes = 0;
        FmIqConsumer::Format format = FmIqConsumer::Format::Iq8;
        uint8_t data[FmIqConsumer::kMaxBlockBytes]{};
    };
    enum class PushResult
    {
        Accepted,
        Invalid,
        Full
    };

    FmIqQueue() = default;
    FmIqQueue(const FmIqQueue&) = delete;
    FmIqQueue& operator=(const FmIqQueue&) = delete;

    // The producer copies only. No allocation, mutex, DSP or audio writes.
    // Full rejects the entire block; the owner must cancel capture rather than
    // silently dropping samples and presenting discontinuous audio as playback.
    PushResult push(uint64_t session, const uint8_t* data, size_t bytes,
                    uint32_t rate, uint32_t generation, uint64_t first_pair,
                    FmIqConsumer::Format format = FmIqConsumer::Format::Iq8)
    {
        const size_t pair_bytes = format == FmIqConsumer::Format::Iq8 ? 2 : 4;
        if ((format != FmIqConsumer::Format::Iq8 && format != FmIqConsumer::Format::Iq16Le) ||
            !data || !bytes || bytes > FmIqConsumer::kMaxBlockBytes || bytes % pair_bytes ||
            rate != BroadcastFmDecoder::kInputRate ||
            first_pair > UINT64_MAX - bytes / pair_bytes)
            return PushResult::Invalid;
        const uint32_t write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == kDepth)
            return PushResult::Full;
        auto& block = blocks_[write % kDepth];
        block.session = session;
        block.first_pair = first_pair;
        block.generation = generation;
        block.rate = rate;
        block.bytes = bytes;
        block.format = format;
        std::memcpy(block.data, data, bytes);
        write_.store(write + 1, std::memory_order_release);
        return PushResult::Accepted;
    }

    // Consumer scratch must also be persistent. Empty leaves it unchanged.
    bool pop(Block& output)
    {
        const uint32_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        const auto& block = blocks_[read % kDepth];
        output.session = block.session;
        output.first_pair = block.first_pair;
        output.generation = block.generation;
        output.rate = block.rate;
        output.bytes = block.bytes;
        output.format = block.format;
        std::memcpy(output.data, block.data, block.bytes);
        read_.store(read + 1, std::memory_order_release);
        return true;
    }

  private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "FM capture queue requires lock-free 32-bit atomics");
    Block blocks_[kDepth]{};
    std::atomic<uint32_t> write_{0};
    std::atomic<uint32_t> read_{0};
};
} // namespace platform::shared::radio
