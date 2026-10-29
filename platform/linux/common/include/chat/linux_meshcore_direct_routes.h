#pragma once
#include "chat/linux_meshcore_direct_wire.h"

namespace trailmate::linux_app::meshcore_wire
{
// Owned by the radio worker. Public-key binding prevents node-ID reuse from
// inheriting an old peer's route. Unsigned ages also cover millis wraparound.
class DirectRoutes
{
  public:
    static constexpr uint32_t lifetime_ms = 10 * 60 * 1000;
    bool remember(const std::array<uint8_t, 32>& peer, const DirectIncoming& message, uint32_t now)
    {
        if (!message.has_advertised_path || message.advertised_path_size > 64 ||
            !chat::meshcore::pathIsWellFormed(message.profile, message.advertised_path_size)) return false;
        Entry* selected = &entries_[0];
        for (auto& entry : entries_)
        {
            if (entry.valid && entry.peer == peer && entry.profile == message.profile)
            {
                selected = &entry;
                break;
            }
            if (!entry.valid || (selected->valid && uint32_t(now - entry.timestamp) > uint32_t(now - selected->timestamp))) selected = &entry;
        }
        *selected = {peer, message.profile, message.advertised_path, message.advertised_path_size, now, true};
        return true;
    }
    bool apply(const std::array<uint8_t, 32>& peer, chat::meshcore::PayloadProfile profile, uint32_t now, Frame& frame)
    {
        for (auto& entry : entries_)
        {
            if (!entry.valid || entry.peer != peer || entry.profile != profile) continue;
            if (uint32_t(now - entry.timestamp) >= lifetime_ms)
            {
                entry.valid = false;
                return false;
            }
            return applyDirectPath(frame, profile, entry.path.data(), entry.size);
        }
        return false;
    }
    void forget(const std::array<uint8_t, 32>& peer)
    {
        for (auto& entry : entries_)
            if (entry.peer == peer) entry.valid = false;
    }
  private:
    struct Entry
    {
        std::array<uint8_t, 32> peer{};
        chat::meshcore::PayloadProfile profile{};
        std::array<uint8_t, 64> path{};
        std::size_t size = 0;
        uint32_t timestamp = 0;
        bool valid = false;
    };
    std::array<Entry, 32> entries_{};
};
} // namespace trailmate::linux_app::meshcore_wire
