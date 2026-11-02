#pragma once
#include "platform/linux/oneseg_iq_archive.h"
#include "platform/linux/oneseg_iq_stream.h"
#include <chrono>
#include <thread>

namespace platform::linux_runtime
{
// Offline source: real signed IQ8, paced by its recorded sample clock. No radio
// lease, RF handoff, SPI access or transmit operations are involved.
inline bool replay_oneseg_iq(const OneSegIqArchive& archive, const OneSegIqRequest& request, OneSegIqResult& result, bool loop)
{
    result = {};
    if (!archive.size() || !archive.metadata().valid() || !request.accept || !request.cancelled ||
        !request.staging || request.staging_bytes < 2) return false;
    const size_t block = request.staging_bytes - request.staging_bytes % 2;
    const auto started = std::chrono::steady_clock::now();
    result.nominal_rate_hz = archive.metadata().rate;
    size_t offset = 0;
    while (!request.cancelled(request.context))
    {
        const auto due = started + std::chrono::microseconds(result.validated_bytes / 2 * 1000000 / result.nominal_rate_hz);
        while (std::chrono::steady_clock::now() < due)
        {
            if (request.cancelled(request.context)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (request.cancelled(request.context)) break;
        const size_t bytes = std::min(block, archive.size() - offset);
        std::memcpy(request.staging, archive.data() + offset, bytes);
        if (!request.accept(request.context, request.staging, bytes, result.validated_bytes / 2))
        {
            result.sink_rejected = true;
            break;
        }
        result.validated_bytes += bytes;
        offset += bytes;
        if (offset == archive.size())
        {
            if (!loop) break;
            offset = 0;
        }
    }
    result.cancelled = request.cancelled(request.context);
    result.elapsed_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    return !result.sink_rejected;
}
} // namespace platform::linux_runtime
