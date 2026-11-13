/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_announce_scheduler.h
 * @brief Local announce TX scheduling state for the embedded LXMF runtime.
 */

#pragma once

#include <cstdint>

namespace chat::lxmf::runtime
{

struct AnnounceScheduleDecision
{
    bool should_send = false;
};

class AnnounceScheduler
{
  public:
    void resetAfterConfig(uint32_t now_ms, bool anonymous_peer);
    bool beginManualBroadcast(bool anonymous_peer);
    void markIdentityChanged();

    AnnounceScheduleDecision next(uint32_t now_ms,
                                  bool anonymous_peer,
                                  uint32_t announce_interval_ms,
                                  uint32_t initial_delay_ms,
                                  uint32_t retry_delay_ms);

    void completeAttempt(uint32_t now_ms, bool any_sent, bool all_complete);

    bool rebroadcastDue(uint32_t now_ms, uint32_t interval_ms);

    [[nodiscard]] bool isPending() const { return pending_; }

  private:
    uint32_t last_announce_ms_ = 0;
    uint32_t last_attempt_ms_ = 0;
    uint32_t last_rebroadcast_ms_ = 0;
    bool pending_ = true;
};

} // namespace chat::lxmf::runtime
