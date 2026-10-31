#pragma once

#include "lr2021_iq.h"
#include "platform/linux/oneseg_iq_stream.h"
#include "platform/linux/oneseg_lr2021_rx.h"
#include <limits>

namespace platform::linux_runtime
{
// Bridge the existing checked packet HAL to the RX-only TV configuration.
// The caller holds its physical/coordinator mutex for this entire operation.
inline int run_oneseg_lr2021_stream(const lr2021_iq_hal& packet, OneSegLr2021Rx::Port& port,
                                    const OneSegIqRequest& request, OneSegIqResult& result)
{
    result = {};
    if (!OneSegLr2021Rx::frequencyHz(request.channel) || !request.staging ||
        request.staging_bytes < 1280 || !request.accept || !request.cancelled)
    {
        result.capture_error = LR2021_IQ_ARGUMENT;
        return LR2021_IQ_ARGUMENT;
    }
    if (request.cancelled(request.context))
    {
        result.cancelled = true;
        return 0; // A page-exit race must not arm RX after cancellation.
    }
    struct Context
    {
        const lr2021_iq_hal& base;
        OneSegLr2021Rx::Port& port;
        const OneSegIqRequest& request;
        lr2021_iq_profile saved{};
        bool setup_attempted = false;
        uint64_t published_bytes = 0;
    } context{packet, port, request};
    lr2021_iq_hal h{};
    h.context = &context;
    h.lock = [](void* c)
    { auto& s = *static_cast<Context*>(c); s.base.lock(s.base.context); };
    h.unlock = [](void* c)
    { auto& s = *static_cast<Context*>(c); s.base.unlock(s.base.context); };
    h.snapshot = [](void* c, lr2021_iq_profile* p)
    {
        auto& s = *static_cast<Context*>(c);
        const int code = s.base.snapshot(s.base.context, &s.saved);
        if (code) return code;
        *p = s.saved;
        p->frequency_hz = OneSegLr2021Rx::frequencyHz(s.request.channel);
        p->route = OneSegLr2021Rx::kRoute;
        p->input_code = OneSegLr2021Rx::kInputCode;
        p->route_rate_hz = OneSegLr2021Rx::kPairRate;
        p->rx_path = 0;
        p->gain_step = p->rx_boost = 0;
        return 0;
    };
    h.read32 = [](void* c, uint32_t a, uint32_t* v)
    { auto& s = *static_cast<Context*>(c); return s.base.read32(s.base.context, a, v); };
    h.write32 = [](void* c, uint32_t a, uint32_t v)
    { auto& s = *static_cast<Context*>(c); return s.base.write32(s.base.context, a, v); };
    h.read = [](void* c, uint16_t op, uint8_t* p, size_t n)
    { auto& s = *static_cast<Context*>(c); return s.base.read(s.base.context, op, p, n); };
    h.write = [](void* c, uint16_t op, const uint8_t* p, size_t n)
    { auto& s = *static_cast<Context*>(c); return s.base.write(s.base.context, op, p, n); };
    h.standby = [](void* c)
    {
        auto& s = *static_cast<Context*>(c);
        if (s.setup_attempted) return s.base.standby(s.base.context);
        s.setup_attempted = true;
        const auto setup = OneSegLr2021Rx::configure(s.port, s.request.channel);
        return setup ? 0 : setup.native_error ? setup.native_error
                                              : LR2021_IQ_STATE;
    };
    h.receive = [](void* c)
    {
        auto& s = *static_cast<Context*>(c);
        const uint8_t continuous[] = {255, 255, 255};
        // Raw RX must not run RadioLib's cached LoRa/GFSK packet-mode staging.
        return s.base.write(s.base.context, 0x020c, continuous, sizeof(continuous));
    };
    h.reset = [](void* c)
    { auto& s = *static_cast<Context*>(c); return s.base.reset(s.base.context); };
    h.restore = [](void* c, const lr2021_iq_profile*)
    {
        auto& s = *static_cast<Context*>(c);
        return s.base.restore(s.base.context, &s.saved);
    };
    h.fast = [](void* c, bool enabled)
    { auto& s = *static_cast<Context*>(c); return s.base.fast(s.base.context, enabled); };
    h.clear_fifo = [](void* c)
    { auto& s = *static_cast<Context*>(c); return s.base.clear_fifo(s.base.context); };
    h.read_fifo = [](void* c, uint8_t* p, size_t n)
    { auto& s = *static_cast<Context*>(c); return s.base.read_fifo(s.base.context, p, n); };
    h.now_us = [](void* c)
    { auto& s = *static_cast<Context*>(c); return s.base.now_us(s.base.context); };
    h.sleep_us = [](void* c, unsigned us)
    { auto& s = *static_cast<Context*>(c); s.base.sleep_us(s.base.context, us); };
    lr2021_iq_session session{};
    session.fifo_capacity = 1024;
    session.buffer = request.staging;
    session.buffer_size = request.staging_bytes;
    session.cancelled = request.cancelled;
    session.cancel_context = request.context;
    lr2021_iq_result native{};
    const auto sink = [](void* c, const uint8_t* data, size_t bytes, uint64_t first_pair)
    {
        auto& s = *static_cast<Context*>(c);
        // Upstream's byte counters are size_t. Extend them here so an armv7
        // stream does not reset the downstream pair index after 4 GiB (~1 h).
        if (first_pair != static_cast<size_t>(s.published_bytes) / 2 || bytes % 2 ||
            s.published_bytes > std::numeric_limits<uint64_t>::max() - bytes) return false;
        if (!s.request.accept(s.request.context, data, bytes, s.published_bytes / 2)) return false;
        s.published_bytes += bytes;
        return true;
    };
    int code = lr2021_iq_stream_session(&h, &session, sink, &context, &native);
    result.validated_bytes = context.published_bytes;
    result.elapsed_us = native.elapsed_us;
    result.max_service_gap_us = native.max_service_gap_us;
    result.nominal_rate_hz = native.nominal_rate_hz;
    result.max_fifo_level = native.max_fifo_level;
    result.fifo_flags = native.fifo_flags;
    result.cancelled = native.cancelled;
    result.sink_rejected = native.stopped_by_sink && !native.cancelled;
    result.capture_error = native.capture_error;
    result.recovery_error = native.recovery_error;
    if (!code && result.sink_rejected)
        code = result.capture_error = LR2021_IQ_DATA;
    return code;
}
} // namespace platform::linux_runtime
