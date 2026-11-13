/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"

namespace trailmate::linux_app
{
// Worker-owned scratch for signed local delivery/telephony advertisements.
class LinuxReticulumAnnounce
{
  public:
    enum class Aspect
    {
        Delivery,
        Telephony
    };
    explicit LinuxReticulumAnnounce(const platform::linux_runtime::LinuxReticulumIdentity& identity) : identity_(identity) {}
    bool build(Aspect, const char* display_name, uint32_t utc_seconds, bool path_response = false);
    const uint8_t* data() const { return packet_.data(); }
    size_t size() const { return size_; }
    void destination(Aspect, uint8_t output[16]) const;

  private:
    const platform::linux_runtime::LinuxReticulumIdentity& identity_;
    std::array<uint8_t, ::chat::reticulum::kReticulumMtu> packet_{}, payload_{}, signed_{};
    size_t size_ = 0;
};
} // namespace trailmate::linux_app
