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

#include "chat/infra/lxmf/lxmf_announce_defaults.h"
#include "chat/linux_reticulum_announce.h"
#include <cstring>
#include <string>

namespace trailmate::linux_app
{
// Owned by the mesh worker. Uses its normal packet sender and never opens a
// second radio or IP interface.
class LinuxReticulumRadioAnnounce
{
  public:
    using Sender = bool (*)(void*, const uint8_t*, size_t);
    explicit LinuxReticulumRadioAnnounce(const platform::linux_runtime::LinuxReticulumIdentity& identity)
        : announce_(identity) {}

    void configure(bool enabled, const std::string& name, uint32_t now)
    {
        const char* effective_name = ::chat::lxmf::announceDisplayName(name.c_str());
        if (enabled_ == enabled && name_ == effective_name) return;
        enabled_ = enabled;
        name_ = effective_name;
        started_ = now;
        interval_ = 5000;
        responded_ = false;
    }

    void tick(uint32_t now, uint32_t utc, Sender send, void* context)
    {
        if (!enabled_ || uint32_t(now - started_) < interval_) return;
        started_ = now;
        interval_ = 30000;
        if (advertise(utc, false, send, context)) interval_ = ::chat::lxmf::kDefaultAnnounceIntervalMs;
    }

    bool receive(const uint8_t* bytes, size_t size, uint32_t now, uint32_t utc, Sender send, void* context)
    {
        if (!enabled_) return false;
        ::chat::reticulum::ParsedPacket packet;
        if (!::chat::reticulum::parsePacket(bytes, size, &packet)) return false;
        uint8_t control[16], local[16];
        ::chat::reticulum::computePathRequestDestinationHash(control);
        if (packet.packet_type != ::chat::reticulum::PacketType::Data ||
            packet.destination_type != ::chat::reticulum::DestinationType::Plain ||
            packet.context != 0 || packet.payload_len < 16 ||
            std::memcmp(packet.destination_hash, control, 16)) return false;
        announce_.destination(LinuxReticulumAnnounce::Aspect::Delivery, local);
        if (std::memcmp(packet.payload, local, 16)) return false;
        if (!responded_ || uint32_t(now - responded_at_) >= 1000)
        {
            if (advertise(utc, true, send, context))
            {
                responded_ = true;
                responded_at_ = now;
            }
        }
        return true;
    }

  private:
    bool advertise(uint32_t utc, bool response, Sender send, void* context)
    {
        return send && announce_.build(LinuxReticulumAnnounce::Aspect::Delivery, name_.c_str(), utc, response) &&
               send(context, announce_.data(), announce_.size());
    }
    LinuxReticulumAnnounce announce_;
    std::string name_;
    uint32_t started_ = 0, interval_ = 5000, responded_at_ = 0;
    bool enabled_ = false, responded_ = false;
};
} // namespace trailmate::linux_app
