#pragma once

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
    struct Entry
    {
        Endpoint endpoint{};
        TcpRetry retry{};
        uint32_t seen = 0;
        bool preferred = false;
    };

    bool observe(const Endpoint& endpoint, uint32_t now, bool restored = false)
    {
        if (!endpoint.port || !endpoint.host[0]) return false;
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
    int select(uint32_t now, CanInstall can_install) const
    {
        // Retain the installed endpoint until it actually yields to backoff.
        if (active_ >= 0 && entries_[active_].retry.ready(now)) return -1;
        int selected = -1;
        for (int i = 0; i < kCapacity; ++i)
        {
            const auto& entry = entries_[i];
            if (i == active_ || !entry.endpoint.port || !entry.retry.ready(now) || !can_install(entry.endpoint)) continue;
            if (selected < 0 || entry.preferred) selected = i;
        }
        return selected;
    }

    const Entry& entry(int index) const { return entries_[index]; }
    void installed(int index) { active_ = index; }
    void reset()
    {
        for (auto& entry : entries_) entry = {};
        active_ = -1;
    }

  private:
    void prefer(int index)
    {
        for (int i = 0; i < kCapacity; ++i) entries_[i].preferred = i == index;
    }
    Entry entries_[kCapacity]{};
    int active_ = -1;
};
static_assert(sizeof(GatewayCandidates) <= 512);
} // namespace chat::reticulum
