#pragma once
#include <cstddef>
#include <cstdint>

namespace platform::linux_runtime
{
struct OneSegIqRequest
{
    unsigned channel = 13;
    // Caller-owned staging, retained until the synchronous worker returns.
    uint8_t* staging = nullptr;
    std::size_t staging_bytes = 0;
    // Copy to a bounded queue only: no waits, DSP, UI or radio/coordinator calls.
    bool (*accept)(void*, const uint8_t*, std::size_t, uint64_t first_pair) = nullptr;
    // Nonblocking, thread-safe cancellation; required even for an empty FIFO.
    bool (*cancelled)(void*) = nullptr;
    void* context = nullptr;
};
struct OneSegIqResult
{
    uint64_t validated_bytes = 0, elapsed_us = 0, max_service_gap_us = 0;
    uint32_t nominal_rate_hz = 0, max_fifo_level = 0;
    uint8_t fifo_flags = 0;
    bool cancelled = false, sink_rejected = false;
    int capture_error = 0, recovery_error = 0;
};
} // namespace platform::linux_runtime
