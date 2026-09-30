#pragma once
#include "geocaching/protocol/discovery.h"
#include <cstring>

namespace geocaching
{
struct DirectoryEntry
{
    Destination discovery;
    Destination delivery;
    std::array<std::uint8_t, 64> public_key{};
    std::array<std::uint8_t, 16> epoch{};
    std::array<char, 41> name{};
    std::uint64_t sequence = 0;
    std::uint64_t last_seen = 0;
    std::uint64_t retry_after = 0;
    bool capabilities_verified = false;
    std::uint8_t max_query_items = 0;
    bool in_flight = false;
};

class DirectoryPool
{
  public:
    static constexpr std::size_t kCapacity = 32;
    DirectoryPool(DirectoryEntry* entries, size_t capacity)
        : entries_(entries), capacity_(entries ? (capacity < kCapacity ? capacity : kCapacity) : 0) {}
    // Caller has authenticated the announce and derived both destination hashes.
    bool observe(const Destination& discovery, const protocol::DirectoryAnnouncement& announce,
                 ByteView public_key, std::uint64_t now)
    {
        if (!public_key.data || public_key.size != 64 || announce.name.size() > 40) return false;
        DirectoryEntry* entry = nullptr;
        for (std::size_t i = 0; i < count_; ++i)
            if (entries_[i].discovery.bytes == discovery.bytes)
            {
                entry = &entries_[i];
                break;
            }
        if (entry)
        {
            if (entry->delivery.bytes != announce.delivery.bytes ||
                std::memcmp(entry->public_key.data(), public_key.data, 64) != 0) return false;
        }
        else
        {
            if (count_ == capacity_) return false;
            entry = &entries_[count_++];
            entry->discovery = discovery;
            entry->delivery = announce.delivery;
            std::memcpy(entry->public_key.data(), public_key.data, 64);
        }
        if (entry->epoch != announce.epoch)
        {
            entry->epoch = announce.epoch;
            entry->sequence = announce.sequence;
        }
        else if (announce.sequence > entry->sequence) entry->sequence = announce.sequence;
        entry->last_seen = now;
        entry->name.fill(0);
        if (!announce.name.empty()) std::memcpy(entry->name.data(), announce.name.data(), announce.name.size());
        return true;
    }

    DirectoryEntry* next(std::uint64_t now, bool require_capabilities)
    {
        for (std::size_t checked = 0; checked < count_; ++checked)
        {
            const auto index = cursor_++ % count_;
            auto& entry = entries_[index];
            if (!entry.in_flight && now >= entry.retry_after &&
                entry.capabilities_verified == require_capabilities) return &entry;
        }
        return nullptr;
    }
    std::size_t size() const { return count_; }

  private:
    DirectoryEntry* entries_;
    size_t capacity_;
    std::size_t count_ = 0;
    std::size_t cursor_ = 0;
};
} // namespace geocaching
