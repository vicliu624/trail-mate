#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ui_map_runtime/map_tiles/map_tile_completion_queue.h"
#include "ui_map_runtime/map_tiles/owned_map_tile_event.h"
#include <cassert>

namespace platform::esp::arduino_common::map_tiles
{
// One worker produces; UI consumes/cancels. Payload creation and destruction
// are outside the mutex. Only fixed-size bookkeeping is performed under it.
class MapTileEventQueue final : public ui::map_tiles::IMapTileEventSink
{
  public:
    using Event = ui::map_tiles::MapTileAsyncEvent;
    using Owned = ui::map_tiles::OwnedMapTileEvent;
    using Copy = Event (*)(const Event&);
    using Queue = ui::map_tiles::MapTileCompletionQueue<Owned, 16>;

    MapTileEventQueue(Copy copy, Owned::Deleter release)
        : copy_(copy), release_(release), mutex_(xSemaphoreCreateMutex()), capacity_changed_(xSemaphoreCreateBinary()) {}
    ~MapTileEventQueue()
    {
        clear();
        if (capacity_changed_) vSemaphoreDelete(capacity_changed_);
        if (mutex_) vSemaphoreDelete(mutex_);
    }
    MapTileEventQueue(const MapTileEventQueue&) = delete;
    MapTileEventQueue& operator=(const MapTileEventQueue&) = delete;

    bool available() const { return mutex_ && capacity_changed_; }

    struct Statistics
    {
        std::size_t occupied = 0;
        std::size_t high_water = 0;
        uint32_t backpressure = 0;
    };
    bool statistics(Statistics& out)
    {
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return false;
        out = {queue_.occupied(), queue_.highWater(), queue_.backpressureCount()};
        xSemaphoreGive(mutex_);
        return true;
    }

    void activateGeneration(uint32_t generation)
    {
        if (!mutex_) return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (active_generation_ != generation)
        {
            queue_.cancelGeneration(active_generation_);
            active_generation_ = generation;
        }
        xSemaphoreGive(mutex_);
        collectDiscarded();
        notifyCapacity();
    }

    void cancelGeneration(uint32_t generation)
    {
        if (!mutex_) return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (active_generation_ == generation) active_generation_ = 0;
        queue_.cancelGeneration(generation);
        xSemaphoreGive(mutex_);
        collectDiscarded();
        notifyCapacity();
    }

    ui::map_tiles::MapTileReservationStatus reserve(const ui::map_tiles::LoadTileCommand& command) override
    {
        using Status = ui::map_tiles::MapTileReservationStatus;
        if (!available()) return Status::Backpressured;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        assert(!reservation_);
        const bool current = active_generation_ != 0 && command.runtime.generation == active_generation_;
        if (current) reservation_ = queue_.reserve(command.runtime.generation);
        const auto result = !current ? Status::Cancelled : reservation_ ? Status::Reserved
                                                                        : Status::Backpressured;
        xSemaphoreGive(mutex_);
        return result;
    }

    bool publish(const Event& event) override
    {
        // Copy may allocate and may turn allocation failure into Failed. It
        // never calls the filesystem, and always returns an owned completion.
        Owned owned(copy_(event), release_);
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool committed = queue_.commit(reservation_, std::move(owned));
        if (committed) reservation_ = {};
        xSemaphoreGive(mutex_);
        return committed;
    }

    void releaseReservation() override
    {
        if (!mutex_) return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (reservation_) queue_.release(reservation_);
        reservation_ = {};
        xSemaphoreGive(mutex_);
        notifyCapacity();
    }

    bool pop(Event& out)
    {
        return popIf(out, [](const Event&)
                     { return true; });
    }

    template <typename Predicate>
    bool popIf(Event& out, Predicate eligible)
    {
        if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return false;
        Owned owned;
        const bool found = queue_.popIf(owned, [&](const Owned& candidate)
                                        { return eligible(candidate.get()); });
        xSemaphoreGive(mutex_);
        if (found)
        {
            out = owned.release();
            notifyCapacity();
        }
        return found;
    }

    void waitForCapacity(TickType_t timeout)
    {
        if (capacity_changed_) xSemaphoreTake(capacity_changed_, timeout);
    }

    void collectDiscarded()
    {
        if (!mutex_) return;
        for (;;)
        {
            Owned discarded;
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool found = queue_.takeDiscarded(discarded);
            xSemaphoreGive(mutex_);
            if (!found) break;
            notifyCapacity();
        }
    }

    void clear()
    {
        if (!mutex_) return;
        activateGeneration(0);
        Event event;
        while (pop(event)) release_(event);
    }

  private:
    void notifyCapacity()
    {
        if (capacity_changed_) xSemaphoreGive(capacity_changed_);
    }
    Copy copy_;
    Owned::Deleter release_;
    SemaphoreHandle_t mutex_;
    SemaphoreHandle_t capacity_changed_;
    Queue queue_{};
    Queue::Reservation reservation_{};
    uint32_t active_generation_ = 0;
};
} // namespace platform::esp::arduino_common::map_tiles
