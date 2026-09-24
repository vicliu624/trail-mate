#pragma once

#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"
#include <array>

namespace ui::map_tiles
{
// A command slot remains owned until its completion is consumed or it is
// cancelled. This is both the dedupe index and the bounded in-flight ledger;
// no second unbounded registry is needed. Platform owns synchronization.
template <std::size_t Capacity>
class MapTileRequestQueue
{
  public:
    void statistics(std::size_t& queued, std::size_t& in_flight) const
    {
        queued = in_flight = 0;
        for (const auto& slot : slots_)
        {
            if (slot.state == State::Queued) ++queued;
            if (slot.state == State::InFlight) ++in_flight;
        }
    }

    TileSubmitResult enqueue(const LoadTileCommand& command)
    {
        Slot* free = nullptr;
        for (auto& slot : slots_)
        {
            if (slot.state == State::Free)
            {
                if (!free) free = &slot;
                continue;
            }
            if (slot.command.runtime.generation == command.runtime.generation && sameTile(slot.command.tile, command.tile))
                return {TileSubmitStatus::AlreadyPending, handle(slot.command)};
        }
        if (!free) return {TileSubmitStatus::Backpressured, {}};
        free->command = command;
        free->state = State::Queued;
        return {TileSubmitStatus::Accepted, handle(command)};
    }

    bool pop(uint32_t now_ms, LoadTileCommand& out)
    {
        (void)now_ms; // Tile requests currently have no deadlines; cancellation is explicit.
        Slot* best = nullptr;
        for (auto& slot : slots_)
        {
            if (slot.state != State::Queued) continue;
            if (!best || slot.command.runtime.priority < best->command.runtime.priority ||
                (slot.command.runtime.priority == best->command.runtime.priority &&
                 static_cast<int32_t>(slot.command.runtime.created_at_ms - best->command.runtime.created_at_ms) < 0))
                best = &slot;
        }
        if (!best) return false;
        best->state = State::InFlight;
        out = best->command;
        return true;
    }

    bool complete(TileRequestHandle request)
    {
        for (auto& slot : slots_)
        {
            if (slot.state == State::InFlight && request.matches(slot.command.runtime.generation, slot.command.runtime.command_id))
            {
                slot.state = State::Free;
                return true;
            }
        }
        return false;
    }

    std::size_t cancelGeneration(uint32_t generation)
    {
        std::size_t removed = 0;
        for (auto& slot : slots_)
        {
            if (slot.state != State::Free && slot.command.runtime.generation == generation)
            {
                slot.state = State::Free;
                ++removed;
            }
        }
        return removed;
    }

  private:
    static TileRequestHandle handle(const LoadTileCommand& command)
    {
        return {command.runtime.generation, command.runtime.command_id};
    }
    static bool sameTile(const MapTileRef& left, const MapTileRef& right)
    {
        return left.layer == right.layer && left.z == right.z && left.x == right.x && left.y == right.y;
    }
    enum class State : uint8_t
    {
        Free,
        Queued,
        InFlight
    };
    struct Slot
    {
        LoadTileCommand command{};
        State state = State::Free;
    };
    std::array<Slot, Capacity> slots_{};
};
} // namespace ui::map_tiles
