#pragma once
#include "geocaching/storage/tx_attempt.h"

namespace geocaching::storage
{
// Expiry means outcome unknown and eligible for policy-controlled retry, not
// proof that the remote side did not receive it. Never reuse the old LXMF hash.
inline bool attemptTimeoutReached(const TxAttemptView& attempt, const StoredTime& now,
                                  uint64_t recovery_started_ms, uint64_t timeout_ms)
{
    if (!timeout_ms || attempt.has_finished) return false;
    if (attempt.submitted.boot_id == now.boot_id)
        return now.monotonic_ms >= attempt.submitted.monotonic_ms &&
               now.monotonic_ms - attempt.submitted.monotonic_ms >= timeout_ms;
    if (attempt.submitted.utc_trusted && attempt.submitted.has_utc && now.utc_trusted && now.has_utc &&
        now.utc_seconds >= attempt.submitted.utc_seconds)
    {
        const auto seconds = timeout_ms / 1000 + (timeout_ms % 1000 != 0);
        return now.utc_seconds - attempt.submitted.utc_seconds >= seconds;
    }
    // No comparable previous-boot clock: grant a full grace period from this
    // recovery lifecycle rather than subtracting unrelated uptime counters.
    return now.monotonic_ms >= recovery_started_ms && now.monotonic_ms - recovery_started_ms >= timeout_ms;
}
} // namespace geocaching::storage
