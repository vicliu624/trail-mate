#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ui::map_tiles
{
// Bounded completion storage. The platform serializes calls; this component
// deliberately knows nothing about RTOS locks, SD transports or payload heaps.
// A reservation counts against capacity before any I/O is started. Cancelled
// reservations remain occupied until their producer acknowledges cancellation.
template <typename T, std::size_t Capacity>
class MapTileCompletionQueue
{
    static_assert(Capacity > 0, "A completion queue needs storage");

  public:
    struct Reservation
    {
        std::size_t slot = Capacity;
        uint32_t epoch = 0;
        explicit operator bool() const { return slot < Capacity && epoch != 0; }
    };

    MapTileCompletionQueue() = default;
    MapTileCompletionQueue(const MapTileCompletionQueue&) = delete;
    MapTileCompletionQueue& operator=(const MapTileCompletionQueue&) = delete;

    Reservation reserve(uint32_t generation)
    {
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            auto& slot = slots_[i];
            if (slot.state != State::Free) continue;
            if (++slot.epoch == 0) ++slot.epoch;
            slot.generation = generation;
            slot.state = State::Reserved;
            ++occupied_;
            if (occupied_ > high_water_) high_water_ = occupied_;
            return {i, slot.epoch};
        }
        ++backpressure_count_;
        return {};
    }

    bool active(Reservation token) const
    {
        return matches(token) && slots_[token.slot].state == State::Reserved;
    }

    // false means cancellation (or a stale token), never queue-full. On false
    // the caller retains payload ownership and must release its reservation.
    bool commit(Reservation token, T&& value)
    {
        if (!active(token)) return false;
        auto& slot = slots_[token.slot];
        slot.value = std::move(value);
        slot.sequence = next_sequence_++;
        slot.state = State::Ready;
        return true;
    }

    // Only the producer releases a reservation. This cannot release a newer
    // reservation after its slot has been recycled (epoch/ABA protection).
    bool release(Reservation token)
    {
        if (!matches(token)) return false;
        auto& slot = slots_[token.slot];
        if (slot.state != State::Reserved && slot.state != State::Cancelled) return false;
        slot.state = State::Free;
        --occupied_;
        return true;
    }

    std::size_t cancelGeneration(uint32_t generation)
    {
        std::size_t count = 0;
        for (auto& slot : slots_)
        {
            if (slot.generation != generation) continue;
            if (slot.state == State::Reserved)
            {
                slot.state = State::Cancelled;
                ++count;
            }
            else if (slot.state == State::Ready)
            {
                slot.state = State::Discarded;
                ++count;
            }
        }
        return count;
    }

    // Discarded payloads are moved to the caller, so expensive heap release
    // can happen outside the queue lock. They do not consume render budget.
    bool takeDiscarded(T& out) { return take(State::Discarded, out); }
    bool pop(T& out) { return take(State::Ready, out); }

    template <typename Predicate>
    bool popIf(T& out, Predicate eligible)
    {
        std::size_t index = Capacity;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (slots_[i].state == State::Ready && eligible(slots_[i].value) &&
                (index == Capacity || static_cast<int32_t>(slots_[i].sequence - slots_[index].sequence) < 0)) index = i;
        }
        if (index == Capacity) return false;
        out = std::move(slots_[index].value);
        slots_[index].value = T{};
        slots_[index].state = State::Free;
        --occupied_;
        return true;
    }

    const T* front() const
    {
        const auto index = oldest(State::Ready);
        return index == Capacity ? nullptr : &slots_[index].value;
    }

    std::size_t occupied() const { return occupied_; }
    std::size_t highWater() const { return high_water_; }
    uint32_t backpressureCount() const { return backpressure_count_; }

  private:
    enum class State : uint8_t
    {
        Free,
        Reserved,
        Ready,
        Cancelled,
        Discarded
    };
    struct Slot
    {
        T value{};
        uint32_t generation = 0;
        uint32_t epoch = 0;
        uint32_t sequence = 0;
        State state = State::Free;
    };

    bool matches(Reservation token) const
    {
        return token && slots_[token.slot].epoch == token.epoch;
    }

    std::size_t oldest(State state) const
    {
        std::size_t found = Capacity;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (slots_[i].state == state &&
                (found == Capacity || static_cast<int32_t>(slots_[i].sequence - slots_[found].sequence) < 0))
                found = i;
        }
        return found;
    }

    bool take(State state, T& out)
    {
        const auto index = oldest(state);
        if (index == Capacity) return false;
        auto& slot = slots_[index];
        out = std::move(slot.value);
        slot.value = T{};
        slot.state = State::Free;
        --occupied_;
        return true;
    }

    std::array<Slot, Capacity> slots_{};
    std::size_t occupied_ = 0;
    std::size_t high_water_ = 0;
    uint32_t next_sequence_ = 0;
    uint32_t backpressure_count_ = 0;
};
} // namespace ui::map_tiles
