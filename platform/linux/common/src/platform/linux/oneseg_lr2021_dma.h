#pragma once

#include "lr2021_iq.h"
#include "platform/linux/oneseg_iq_stream.h"
#include "platform/linux/oneseg_lr2021_rx.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace platform::linux_runtime
{
// Experimental CM0 kernel-DMA path. At 500 kpair/s, IQ arrives at 1 MB/s;
// the selected SPI clock drains slightly faster. Read length is bounded by
// the maximum withdrawal from the initial reserve at that requested clock;
// the remainder arrives during DMA.
// Choose each read from the measured level while retaining 32 bytes, then
// require clean sticky FIFO flags before publishing. No level is fabricated.
// Setup/recovery registers and route semantics follow the pinned MIT RX engine.
inline int run_oneseg_lr2021_dma(const lr2021_iq_hal& h, OneSegLr2021Rx::Port& port,
                                 const OneSegIqRequest& request, OneSegIqResult& result, unsigned gain_step,
                                 uint32_t sample_rate, uint8_t* dma_staging, size_t dma_capacity)
{
    result = {};
    if ((sample_rate != 450000 && sample_rate != 500000) || !request.staging || request.staging_bytes < 2 || !dma_staging || dma_capacity < 32764 || !request.accept ||
        !request.cancelled || !h.lock || !h.unlock || !h.snapshot || !h.fast || !h.read || !h.write ||
        !h.read32 || !h.write32 || !h.clear_fifo || !h.read_fifo || !h.standby || !h.reset ||
        !h.restore || !h.now_us || !h.sleep_us)
        return result.capture_error = LR2021_IQ_ARGUMENT;
    if (request.cancelled(request.context))
    {
        result.cancelled = true;
        return 0;
    }
    void* c = h.context;
    h.lock(c);
    lr2021_iq_profile saved{};
    int error = h.snapshot(c, &saved);
    if (!error && (!saved.lr2021 || !saved.configured || !saved.exclusive_lora ||
                   saved.fw_major != 1 || saved.fw_minor != 24)) error = LR2021_IQ_STATE;
    uint32_t magic = 0;
    if (!error) error = h.read32(c, 0x800ff8, &magic);
    if (!error && magic == 0x600db002) error = LR2021_IQ_STATE;
    const bool touched = !error;
    bool fast = false;
    if (!error)
    {
        error = h.fast(c, true);
        fast = !error;
    }
    const uint32_t spi_clock = port.spiClockHz();
    if (!error && spi_clock != 8350000 && spi_clock != 8700000 && spi_clock != 16000000) error = LR2021_IQ_ARGUMENT;
    if (!error)
    {
        const auto setup = OneSegLr2021Rx::configure(port, request.channel, gain_step, sample_rate);
        if (!setup) error = setup.native_error ? setup.native_error : LR2021_IQ_STATE;
    }
    // Conservative initial-reserve withdrawal at the requested SPI clock:
    // floor(1 / (1 - input_bytes_per_second / spi_bytes_per_second)).
    const size_t reserve_factor = spi_clock > sample_rate * 16
                                      ? spi_clock / (spi_clock - sample_rate * 16)
                                      : 0;
    const size_t minimum_catchup = spi_clock == 16000000 ? 512 : spi_clock == 8700000 ? 1536
                                                                                      : 2944;
    const size_t maximum_read = spi_clock == 16000000 ? 2044 : 16380;
    const uint8_t route[] = {OneSegLr2021Rx::kInputCode, static_cast<uint8_t>(sample_rate >> 24),
                             static_cast<uint8_t>(sample_rate >> 16), static_cast<uint8_t>(sample_rate >> 8),
                             static_cast<uint8_t>(sample_rate), 8, 0, 0, 0, 0, 1};
    const uint8_t clear[] = {0x3f, 0x3f}, receive[] = {0xff, 0xff, 0xff};
    const auto verify = [&]
    {
        uint32_t base = 0, size = 0;
        int code = h.read32(c, 0xf30028, &base);
        if (!code) code = h.read32(c, 0xf30030, &size);
        return code ? code : ((base == 0x804400 || base == 0x4400) && size == 0x3e8 ? 0 : LR2021_IQ_DATA);
    };
    if (!error) error = h.write(c, 0x0290, route, sizeof(route));
    if (!error) error = h.write32(c, 0xf30028, 0x804400);
    if (!error) error = h.write32(c, 0xf30030, 0x3e8);
    if (!error) error = verify();
    if (!error) error = h.write(c, 0x0114, clear, sizeof(clear));
    if (!error) error = h.write(c, 0x020c, receive, sizeof(receive));
    if (!error) error = verify();
    if (!error) error = h.clear_fifo(c);
    if (!error) error = h.write(c, 0x0114, clear, sizeof(clear));
    const uint64_t start = h.now_us(c);
    uint64_t previous = start;
    struct Trace
    {
        unsigned level = 0, bytes = 0, extra_bytes = 0;
        uint64_t query_us = 0, spi_us = 0, flags_us = 0, publish_us = 0;
        unsigned flags = 0;
    };
    std::array<Trace, 8> traces{};
    unsigned trace_count = 0;
    unsigned catchup_reads = 0;
    unsigned idle_waits = 0;
    uint64_t max_idle_wait_us = 0, max_publish_us = 0;
    while (!error)
    {
        if (request.cancelled(request.context))
        {
            result.cancelled = true;
            break;
        }
        const uint64_t now = h.now_us(c), gap = now - previous;
        previous = now;
        if (gap > result.max_service_gap_us) result.max_service_gap_us = gap;
        uint8_t level_bytes[2]{};
        const uint64_t query_start = h.now_us(c);
        error = h.read(c, 0x011c, level_bytes, sizeof(level_bytes));
        const uint64_t query_end = h.now_us(c);
        const unsigned level = unsigned(level_bytes[0]) << 8 | level_bytes[1];
        if (level > result.max_fifo_level) result.max_fifo_level = level;
        if (!error && level >= 1024) error = LR2021_IQ_DATA;
        if (error) break;
        if (level < 320)
        {
            const uint64_t waiting = h.now_us(c);
            h.sleep_us(c, 50);
            ++idle_waits;
            max_idle_wait_us = std::max(max_idle_wait_us, h.now_us(c) - waiting);
            continue;
        }
        size_t bytes = std::min(maximum_read, size_t(level - 32) * reserve_factor) & ~size_t{3};
        auto& trace = traces[trace_count++ % traces.size()];
        trace = {};
        trace.level = level;
        trace.bytes = static_cast<unsigned>(bytes);
        trace.query_us = query_end - query_start;
        const uint64_t spi_start = h.now_us(c);
        error = h.read_fifo(c, dma_staging, bytes);
        while (!error && !request.cancelled(request.context))
        {
            // A delayed completion can leave a valid first read but too little
            // reserve for flags, publication and another level query. Estimate
            // arrivals only over this short measured interval, keeping 128 bytes
            // for clock/measurement error. Stage bounded catch-up reads before
            // checking sticky flags for the whole batch. The estimate is never exposed
            // as a measured FIFO level and no unvalidated samples are published.
            const uint64_t arrived = (h.now_us(c) - query_end) * uint64_t(sample_rate) * 2 / 1000000;
            const uint64_t estimated = uint64_t(level) + arrived > bytes ? uint64_t(level) + arrived - bytes : 0;
            // Leave room for the flags query, chunk publication, next level
            // query and DMA startup. Waiting for 512 bytes can start the next
            // transfer near a full FIFO even though this read was clean.
            if (estimated >= 256)
            {
                const size_t extra = static_cast<size_t>(std::min({uint64_t(maximum_read), (estimated - 128) * reserve_factor,
                                                                   uint64_t(dma_capacity - bytes)})) &
                                     ~size_t{3};
                // Every iteration consumes finite private staging capacity.
                // Avoid tiny reads whose setup overhead defeats catch-up.
                if (extra < minimum_catchup) break;
                error = h.read_fifo(c, dma_staging + bytes, extra);
                if (!error) ++catchup_reads;
                bytes += extra;
                trace.extra_bytes += static_cast<unsigned>(extra);
            }
            else break;
        }
        trace.bytes = static_cast<unsigned>(bytes);
        const uint64_t spi_end = h.now_us(c);
        uint8_t flags[2]{};
        if (!error) error = h.read(c, 0x011b, flags, sizeof(flags));
        trace.spi_us = spi_end - spi_start;
        trace.flags_us = h.now_us(c) - spi_end;
        trace.flags = flags[0];
        result.fifo_flags |= flags[0];
        if (!error && (flags[0] & 0x30)) error = LR2021_IQ_DATA;
        if (error) break;
        if (request.cancelled(request.context))
        {
            result.cancelled = true;
            break;
        }
        const uint64_t publish_start = h.now_us(c);
        for (size_t offset = 0; offset < bytes;)
        {
            const size_t chunk = std::min(bytes - offset, request.staging_bytes & ~size_t{1});
            if (!request.accept(request.context, dma_staging + offset, chunk, result.validated_bytes / 2))
            {
                result.cancelled = request.cancelled(request.context);
                result.sink_rejected = !result.cancelled;
                if (result.sink_rejected) error = LR2021_IQ_DATA;
                break;
            }
            result.validated_bytes += chunk;
            offset += chunk;
        }
        trace.publish_us = h.now_us(c) - publish_start;
        max_publish_us = std::max(max_publish_us, trace.publish_us);
        if (result.cancelled || result.sink_rejected) break;
    }
    result.elapsed_us = h.now_us(c) - start;
    result.nominal_rate_hz = sample_rate;
    if (touched)
    {
        uint8_t flags[2]{};
        const int read_flags = h.read(c, 0x011b, flags, sizeof(flags));
        result.fifo_flags |= flags[0];
        const int stopped = h.standby(c);
        if (!error) error = read_flags ? read_flags : stopped;
        if (!error && (flags[0] & 0x30)) error = LR2021_IQ_DATA;
        const int bus = fast ? h.fast(c, false) : 0;
        result.recovery_error = h.reset(c);
        if (!result.recovery_error) result.recovery_error = h.restore(c, &saved);
        if (!result.recovery_error) result.recovery_error = bus;
    }
    result.capture_error = error;
    if (std::getenv("TRAIL_MATE_ONESEG_DMA_TRACE"))
    {
        std::fprintf(stderr, "[oneseg_dma_summary] batches=%u catchup_reads=%u validated_bytes=%llu fifo_flags=%u capture_error=%d recovery_error=%d idle_waits=%u max_idle_wait_us=%llu max_publish_us=%llu\n",
                     trace_count, catchup_reads, (unsigned long long)result.validated_bytes, unsigned(result.fifo_flags),
                     result.capture_error, result.recovery_error, idle_waits,
                     (unsigned long long)max_idle_wait_us, (unsigned long long)max_publish_us);
        for (unsigned i = trace_count > traces.size() ? trace_count - static_cast<unsigned>(traces.size()) : 0; i < trace_count; ++i)
        {
            const auto& trace = traces[i % traces.size()];
            std::fprintf(stderr, "[oneseg_dma] level=%u bytes=%u extra_bytes=%u query_us=%llu spi_us=%llu flags_us=%llu flags=%u publish_us=%llu\n",
                         trace.level, trace.bytes, trace.extra_bytes, (unsigned long long)trace.query_us, (unsigned long long)trace.spi_us,
                         (unsigned long long)trace.flags_us, trace.flags, (unsigned long long)trace.publish_us);
        }
    }
    h.unlock(c);
    return error ? error : result.recovery_error;
}
} // namespace platform::linux_runtime
