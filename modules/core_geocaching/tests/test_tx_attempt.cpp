#include "geocaching/storage/tx_attempt.h"
int main()
{
    using namespace geocaching::storage;
    uint8_t key[64]{}, bytes[192]{}, hash[32]{}; size_t size = 0;
    key[48] = 3;
    TxAttemptView value, out;
    value.submitted.monotonic_ms = 100;
    if (!encodeTxAttempt({key, 64}, value, bytes, sizeof(bytes), size) ||
        !decodeTxAttempt({key, 64}, {bytes, size}, out) || out.attempt_id.data[0] != 3 || out.has_finished) return 1;
    value.state = TxAttemptState::Delivered;
    if (encodeTxAttempt({key, 64}, value, bytes, sizeof(bytes), size) || size) return 2;
    value.has_finished = true; value.finished.monotonic_ms = 200; value.lxmf_hash = {hash, 32};
    if (!encodeTxAttempt({key, 64}, value, bytes, sizeof(bytes), size) ||
        !decodeTxAttempt({key, 64}, {bytes, size}, out) || !out.has_finished || out.lxmf_hash.size != 32) return 3;
    for (size_t n = 0; n < size; ++n)
        if (decodeTxAttempt({key, 64}, {bytes, n}, out) || out.request_key.data) return 4;
    value.state = TxAttemptState::InFlight;
    if (encodeTxAttempt({key, 64}, value, bytes, sizeof(bytes), size)) return 5;
    return 0;
}
