#pragma once

#include "chat/infra/reticulum/public_gateway_host.h"
#include "chat/infra/reticulum/tcp_retry.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/native_gateway_discovery.h"
#include <cstring>

namespace chat::reticulum
{
// Public endpoints only. Embedded in the PSRAM adapter, with no socket ownership.
class GatewayCandidates
{
  public:
    using Endpoint = NativeGatewayDiscovery::Endpoint;
    static constexpr int kCapacity = 4;
    static constexpr uint32_t kDayMs = 86400000U;
    static constexpr uint32_t kExpireMs = 7 * kDayMs;
    struct Entry
    {
        Endpoint endpoint{};
        TcpRetry retry{};
        uint32_t seen = 0;
        bool preferred = false;
    };

    bool observe(const Endpoint& endpoint, uint32_t now, bool restored = false)
    {
        const auto* end = static_cast<const char*>(std::memchr(endpoint.host, 0, sizeof(endpoint.host)));
        if (!endpoint.port || !end || !publicGatewayHost(endpoint.host, static_cast<size_t>(end - endpoint.host))) return false;
        maintain(now);
        int slot = -1;
        for (int i = 0; i < kCapacity; ++i)
        {
            if (entries_[i].endpoint.port == endpoint.port &&
                std::strcmp(entries_[i].endpoint.host, endpoint.host) == 0)
            {
                if (!restored) entries_[i].seen = now;
                if (restored) prefer(i);
                return true; // Announcements never reset endpoint backoff.
            }
            if (!entries_[i].endpoint.port && slot < 0) slot = i;
        }
        if (slot < 0)
        {
            for (int i = 0; i < kCapacity; ++i)
            {
                // Do not forget a cooldown merely because new peers announce.
                if (i == active_ || entries_[i].preferred || !entries_[i].retry.ready(now)) continue;
                if (slot < 0 || uint32_t(now - entries_[i].seen) > uint32_t(now - entries_[slot].seen)) slot = i;
            }
        }
        if (slot < 0) return false;
        entries_[slot] = {};
        entries_[slot].endpoint = endpoint;
        entries_[slot].seen = now;
        if (restored) prefer(slot);
        return true;
    }

    void sync(const TcpRetry& retry)
    {
        if (active_ >= 0) entries_[active_].retry = retry;
    }

    template <typename CanInstall>
    int select(uint32_t now, CanInstall can_install)
    {
        maintain(now);
        // Keep a connected uplink. An expired cooldown is only permission to
        // retry, not evidence that a repeatedly failing endpoint is healthy.
        if (active_ >= 0 && entries_[active_].retry.online()) return -1;
        int selected = active_ >= 0 && entries_[active_].retry.ready(now) ? active_ : -1;
        for (int i = 0; i < kCapacity; ++i)
        {
            const auto& entry = entries_[i];
            if (i == active_ || !entry.endpoint.port || !entry.retry.ready(now) || !can_install(entry.endpoint)) continue;
            if (selected < 0 || entry.retry.failures() < entries_[selected].retry.failures() ||
                (entry.retry.failures() == entries_[selected].retry.failures() &&
                 (ageRank(entry, now) < ageRank(entries_[selected], now) ||
                  (ageRank(entry, now) == ageRank(entries_[selected], now) && entry.preferred && !entries_[selected].preferred)))) selected = i;
        }
        return selected == active_ ? -1 : selected;
    }

    const Entry& entry(int index) const { return entries_[index]; }
    void installed(int index) { active_ = index; }
    void reset()
    {
        for (auto& entry : entries_) entry = {};
        active_ = -1;
    }

  private:
    static unsigned ageRank(const Entry& entry, uint32_t now)
    {
        const auto age = uint32_t(now - entry.seen);
        return age >= 3 * kDayMs ? 2 : age >= kDayMs ? 1
                                                     : 0;
    }
    void maintain(uint32_t now)
    {
        for (int i = 0; i < kCapacity; ++i)
        {
            auto& entry = entries_[i];
            if (!entry.endpoint.port || uint32_t(now - entry.seen) < kExpireMs) continue;
            if (i != active_ && !entry.preferred && entry.retry.ready(now)) entry = {};
            // Keep protected entries stale across the millisecond clock wrap.
            // Only a verified live announcement makes them fresh again.
            else entry.seen = now - kExpireMs;
        }
    }
    void prefer(int index)
    {
        for (int i = 0; i < kCapacity; ++i) entries_[i].preferred = i == index;
    }
    Entry entries_[kCapacity]{};
    int active_ = -1;
};
static_assert(sizeof(GatewayCandidates) <= 512);
} // namespace chat::reticulum
