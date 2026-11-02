#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace platform::linux_runtime
{
// Keep GPIO lookup state inside the transport's locked allocation. RadioLib
// requests only configured GPIO lines; no allocator call belongs in that path.
class LinuxGpioHandles
{
  public:
    static constexpr size_t capacity = 64;
    using Entry = std::pair<uint32_t, int>;
    Entry* begin() { return entries_.data(); }
    Entry* end() { return begin() + used_; }
    const Entry* begin() const { return entries_.data(); }
    const Entry* end() const { return begin() + used_; }
    Entry* find(uint32_t pin)
    {
        for (auto* entry = begin(); entry != end(); ++entry)
            if (entry->first == pin) return entry;
        return end();
    }
    const Entry* find(uint32_t pin) const
    {
        for (auto* entry = begin(); entry != end(); ++entry)
            if (entry->first == pin) return entry;
        return end();
    }
    size_t count(uint32_t pin) const { return find(pin) != end() ? 1 : 0; }
    int& operator[](uint32_t pin)
    {
        if (auto* entry = find(pin); entry != end()) return entry->second;
        if (used_ == capacity) throw std::length_error("Too many radio GPIO handles");
        entries_[used_] = {pin, -1};
        return entries_[used_++].second;
    }
    void clear() { used_ = 0; }

  private:
    std::array<Entry, capacity> entries_{};
    size_t used_ = 0;
};
} // namespace platform::linux_runtime
