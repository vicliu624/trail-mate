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
 * @file lxmf_announce_scheduler.cpp
 * @brief Local announce TX scheduling state for the embedded LXMF runtime.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_scheduler.h"

namespace chat::lxmf::runtime
{

void AnnounceScheduler::resetAfterConfig(uint32_t now_ms, bool anonymous_peer)
{
    last_announce_ms_ = now_ms;
    last_attempt_ms_ = 0;
    pending_ = !anonymous_peer;
}

bool AnnounceScheduler::beginManualBroadcast(bool anonymous_peer)
{
    if (anonymous_peer)
    {
        pending_ = false;
        return false;
    }
    pending_ = true;
    return true;
}

void AnnounceScheduler::markIdentityChanged()
{
    last_attempt_ms_ = 0;
    pending_ = true;
}

AnnounceScheduleDecision AnnounceScheduler::next(uint32_t now_ms,
                                                 bool anonymous_peer,
                                                 uint32_t announce_interval_ms,
                                                 uint32_t initial_delay_ms,
                                                 uint32_t retry_delay_ms)
{
    if (anonymous_peer)
    {
        pending_ = false;
        last_attempt_ms_ = 0;
        return {};
    }

    if (!pending_ && (now_ms - last_announce_ms_) < announce_interval_ms)
    {
        return {};
    }
    if (pending_)
    {
        const bool first_attempt = last_attempt_ms_ == 0;
        const uint32_t wait_ms = first_attempt ? initial_delay_ms : retry_delay_ms;
        const uint32_t basis_ms =
            first_attempt ? last_announce_ms_ : last_attempt_ms_;
        if ((now_ms - basis_ms) < wait_ms)
        {
            return {};
        }
    }

    AnnounceScheduleDecision decision{};
    decision.should_send = true;
    return decision;
}

void AnnounceScheduler::completeAttempt(uint32_t now_ms,
                                        bool any_sent,
                                        bool all_complete)
{
    last_attempt_ms_ = now_ms;
    if (any_sent)
    {
        last_announce_ms_ = now_ms;
    }
    pending_ = !all_complete;
    if (!pending_)
    {
        last_attempt_ms_ = 0;
    }
}

bool AnnounceScheduler::rebroadcastDue(uint32_t now_ms, uint32_t interval_ms)
{
    if (last_rebroadcast_ms_ != 0 &&
        (now_ms - last_rebroadcast_ms_) < interval_ms)
    {
        return false;
    }
    last_rebroadcast_ms_ = now_ms;
    return true;
}

} // namespace chat::lxmf::runtime
