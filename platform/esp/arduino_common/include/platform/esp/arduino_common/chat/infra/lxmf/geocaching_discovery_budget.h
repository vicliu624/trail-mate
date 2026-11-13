/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_budget.h"
#include <array>
#include <cstring>

namespace chat::lxmf::runtime
{
// Admission only: an admitted announcement must still pass the normal
// destination/identity/signature validation before reaching an application.
class GeocachingDiscoveryBudget
{
  public:
    static bool matches(const reticulum::ParsedPacket& packet, bool listening,
                        const RuntimeBudget& budget)
    {
        return matchesApplications(packet, listening, false, budget);
    }
    // Both applications share the existing four-announcement window. Track
    // uses read-only service hashes instead of a second mutable resident cache.
    static bool matchesApplications(const reticulum::ParsedPacket& packet, bool geocaching, bool tracks,
                                    const RuntimeBudget& budget)
    {
        if ((!geocaching && !tracks) || !packet.valid || !packet.destination_hash ||
            packet.packet_type != reticulum::PacketType::Announce ||
            packet.destination_type != reticulum::DestinationType::Single ||
            !budget.phase || std::strcmp(budget.phase, "call") == 0 ||
            std::strcmp(budget.phase, "nomad") == 0 ||
            std::strcmp(budget.phase, "saver") == 0)
            return false;
        reticulum::ParsedAnnounce announce{};
        if (!reticulum::parseAnnounce(packet, &announce) ||
            !announce.name_hash || !announce.app_data_len || announce.app_data_len > 128)
            return false;
        // SHA-256(full service name)[:10], verified against the actual
        // Reticulum computeNameHash implementation in discovery admission tests.
        static constexpr std::array<uint8_t, reticulum::kNameHashSize> name{
            0xe9, 0xef, 0xf7, 0x7f, 0x95, 0xaf, 0xde, 0xba, 0x4e, 0x0c};
        if (geocaching && announce.app_data_len <= 96 && std::memcmp(name.data(), announce.name_hash, name.size()) == 0) return true;
        if (!tracks) return false;
        static constexpr std::array<uint8_t, reticulum::kNameHashSize> track_name{
            0x68, 0xd6, 0x34, 0x81, 0xa7, 0x36, 0xf9, 0xc3, 0x18, 0xad};
        return std::memcmp(track_name.data(), announce.name_hash, track_name.size()) == 0;
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
