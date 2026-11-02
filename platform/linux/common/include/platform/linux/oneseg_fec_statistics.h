#pragma once
#include "platform/linux/oneseg_ofdm_lab.h"

namespace platform::linux_runtime
{
struct OneSegFecStatistics
{
    uint64_t words = 0, clean = 0, corrected = 0, uncorrectable = 0, symbols = 0, maximum = 0;
    uint64_t received_ms = 0;
    bool valid() const
    {
        if (!words || clean > words || corrected > words - clean || uncorrectable != words - clean - corrected || maximum > 8) return false;
        if (!corrected) return symbols == 0 && maximum == 0;
        if (!maximum || symbols < corrected || symbols < maximum) return false;
        return symbols / corrected + (symbols % corrected != 0) <= maximum;
    }
};
struct OneSegFecHistory
{
    static constexpr size_t capacity = 64;
    OneSegLabSource source = OneSegLabSource::Unavailable;
    std::array<OneSegFecStatistics, capacity> samples{};
    size_t size = 0, next = 0;
    const OneSegFecStatistics& newest() const { return samples[(next + capacity - 1) % capacity]; }
    const OneSegFecStatistics& chronological(size_t index) const { return samples[(next + capacity - size + index) % capacity]; }
    bool append(OneSegFecStatistics value, OneSegLabSource origin, uint64_t now)
    {
        if (!value.valid() || origin == OneSegLabSource::Unavailable || origin == OneSegLabSource::Synthetic || (size && source != origin)) return false;
        if (size)
        {
            const auto& old = newest();
            if (value.words <= old.words || value.clean < old.clean || value.corrected < old.corrected ||
                value.uncorrectable < old.uncorrectable || value.symbols < old.symbols || value.maximum < old.maximum) return false;
        }
        value.received_ms = now;
        source = origin;
        samples[next] = value;
        next = (next + 1) % capacity;
        if (size < capacity) ++size;
        return true;
    }
    bool fresh(uint64_t now) const { return size && now >= newest().received_ms && now - newest().received_ms <= 1500; }
};
} // namespace platform::linux_runtime
