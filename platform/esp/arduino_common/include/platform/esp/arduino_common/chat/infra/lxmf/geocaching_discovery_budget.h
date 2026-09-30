#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_budget.h"
#include <array>
#include <cstring>

namespace chat::lxmf::runtime
{
// Admission only: an admitted announcement must still pass the normal
// destination/identity/signature validation before reaching Geocaching.
class GeocachingDiscoveryBudget
{
  public:
    static bool matches(const reticulum::ParsedPacket& packet, bool listening,
                        const RuntimeBudget& budget)
    {
        if (!listening || !packet.valid || !packet.destination_hash ||
            packet.packet_type != reticulum::PacketType::Announce ||
            packet.destination_type != reticulum::DestinationType::Single ||
            !budget.phase || std::strcmp(budget.phase, "call") == 0 ||
            std::strcmp(budget.phase, "nomad") == 0 ||
            std::strcmp(budget.phase, "saver") == 0)
            return false;
        reticulum::ParsedAnnounce announce{};
        if (!reticulum::parseAnnounce(packet, &announce) ||
            !announce.name_hash || !announce.app_data_len || announce.app_data_len > 96)
            return false;
        static const auto name = []
        {
            std::array<uint8_t, reticulum::kNameHashSize> value{};
            reticulum::computeNameHash("trailmate", "geocache.directory", value.data());
            return value;
        }();
        return std::memcmp(name.data(), announce.name_hash, name.size()) == 0;
    }

    bool consume(uint32_t now_ms)
    {
        if (!started_ || static_cast<uint32_t>(now_ms - since_) >= 10000U)
        {
            started_ = true;
            since_ = now_ms;
            used_ = 0;
        }
        if (used_ >= 4) return false;
        ++used_;
        return true;
    }

  private:
    uint32_t since_ = 0;
    uint8_t used_ = 0;
    bool started_ = false;
};
static_assert(sizeof(GeocachingDiscoveryBudget) <= 8);
} // namespace chat::lxmf::runtime
