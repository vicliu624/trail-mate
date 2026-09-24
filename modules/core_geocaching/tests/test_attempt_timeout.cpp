#include "geocaching/storage/attempt_timeout.h"
int main()
{
    using namespace geocaching::storage;
    TxAttemptView attempt; StoredTime now;
    attempt.submitted.monotonic_ms = 100;
    now.monotonic_ms = 1099;
    if (attemptTimeoutReached(attempt, now, 0, 1000)) return 1;
    now.monotonic_ms = 1100;
    if (!attemptTimeoutReached(attempt, now, 0, 1000)) return 2;
    now.boot_id[0] = 1; now.monotonic_ms = 5000;
    if (attemptTimeoutReached(attempt, now, 4500, 1000)) return 3;
    now.monotonic_ms = 5500;
    if (!attemptTimeoutReached(attempt, now, 4500, 1000)) return 4;
    attempt.submitted.has_utc = attempt.submitted.utc_trusted = now.has_utc = now.utc_trusted = true;
    attempt.submitted.utc_seconds = 100; now.utc_seconds = 101;
    if (attemptTimeoutReached(attempt, now, 0, 1001)) return 5;
    now.utc_seconds = 102;
    if (!attemptTimeoutReached(attempt, now, 0, 1001)) return 6;
    attempt.has_finished = true;
    if (attemptTimeoutReached(attempt, now, 0, 1000)) return 7;
    return 0;
}
