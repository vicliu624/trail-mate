#pragma once

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_budget.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace chat::lxmf::runtime
{
// Discovery destinations, not trusted directory identities or TCP endpoints.
// Responses still pass the ordinary announcement and service metadata checks.
// Preserve the directory identity when moving the service to another host.
inline constexpr std::array<uint8_t, 16> kGeocachingDiscoverySeed{
    0x07, 0x84, 0xba, 0x38, 0x02, 0xf0, 0xc3, 0xfb,
    0x8a, 0x63, 0x7e, 0x22, 0x37, 0xbf, 0xb3, 0x62};

class GeocachingDiscoveryProbe
{
  public:
    void reset() { *this = {}; }

    // Called in the network runtime, never in the UI callback. An unsuccessful
    // send also consumes this interval so a failing interface cannot busy-loop.
    bool take(uint32_t now, bool listening, bool ready, const RuntimeBudget& budget)
    {
        if (!listening)
        {
            reset();
            return false;
        }
        if (!ready || !budget.phase || std::strcmp(budget.phase, "call") == 0 ||
            std::strcmp(budget.phase, "nomad") == 0 || std::strcmp(budget.phase, "saver") == 0)
            return false;
        if (started_ && static_cast<uint32_t>(now - last_) < 60000U)
            return false;
        started_ = true;
        last_ = now;
        return true;
    }

  private:
    uint32_t last_ = 0;
    bool started_ = false;
};
static_assert(sizeof(GeocachingDiscoveryProbe) <= 8);
} // namespace chat::lxmf::runtime
