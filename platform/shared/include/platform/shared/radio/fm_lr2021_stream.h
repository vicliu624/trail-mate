#pragma once
#include "lr2021_iq.h"
#include "platform/shared/radio/fm_iq_stream.h"
#include <limits>

namespace platform::shared::radio
{
// The board adapter supplies checked SPI/BUSY operations and physical ownership.
// Setup, streaming, reset and saved packet-PHY restore run under that ownership.
// FIFO callbacks must only copy into a bounded consumer queue.
inline int run_fm_lr2021_stream(const lr2021_iq_hal& packet, FmLr2021Rx::Port& port,
                                const FmIqRequest& request, FmIqResult& result)
{
    result = {};
    if (!packet.lock || !packet.unlock || !packet.snapshot || !packet.read32 || !packet.write32 ||
        !packet.read || !packet.write || !packet.standby || !packet.reset || !packet.restore ||
        !packet.fast || !packet.clear_fifo || !packet.read_fifo || !packet.now_us || !packet.sleep_us ||
        !FmLr2021Rx::frequencySupported(request.frequency_hz) || !request.staging ||
        request.staging_bytes < 1280 || !request.accept || !request.cancelled)
        return result.capture_error = LR2021_IQ_ARGUMENT;
    if (request.cancelled(request.context))
    {
        result.cancelled = true;
        return 0;
    }
    struct Context
    {
        const lr2021_iq_hal& base;
        FmLr2021Rx::Port& port;
        const FmIqRequest& request;
        FmIqResult& result;
        lr2021_iq_profile saved{};
        bool setup_attempted = false;
        uint64_t delivered = 0;
    } context{packet, port, request, result};
    lr2021_iq_hal h{};
    h.context = &context;
    h.lock = [](void* c)
    { auto& s = *static_cast<Context*>(c); s.base.lock(s.base.context); };
    h.unlock = [](void* c)
    { auto& s = *static_cast<Context*>(c); s.base.unlock(s.base.context); };
    h.snapshot = [](void* c, lr2021_iq_profile* p) -> int
    {
        auto& s = *static_cast<Context*>(c);
        const int code = s.base.snapshot(s.base.context, &s.saved);
        if (code) return code;
        if (!s.port.exclusivelyOwned()) return LR2021_IQ_STATE;
        *p = s.saved;
        p->frequency_hz = s.request.frequency_hz;
        p->route = FmLr2021Rx::kRoute;
        p->input_code = FmLr2021Rx::kInputCode;
        p->route_rate_hz = FmLr2021Rx::kPairRate;
        p->rx_path = p->gain_step = p->rx_boost = 0;
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
        s.result.setup_attempted = true;
        s.result.setup = FmLr2021Rx::configure(s.port, s.request.frequency_hz, s.request.allow_image_calibration_warning);
        return s.result.setup ? 0 : s.result.setup.native_error ? s.result.setup.native_error
                                                                : LR2021_IQ_STATE;
    };
    h.receive = [](void* c)
    {
        auto& s = *static_cast<Context*>(c);
        const uint8_t continuous[] = {255, 255, 255};
        return s.base.write(s.base.context, 0x020c, continuous, sizeof(continuous));
    };
    h.reset = [](void* c)
    { auto& s = *static_cast<Context*>(c); return s.base.reset(s.base.context); };
    h.restore = [](void* c, const lr2021_iq_profile*)
    { auto& s = *static_cast<Context*>(c); return s.base.restore(s.base.context, &s.saved); };
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
        if (bytes % 2 || first_pair != static_cast<size_t>(s.delivered) / 2 ||
            s.delivered > std::numeric_limits<uint64_t>::max() - bytes) return false;
        if (!s.request.accept(s.request.context, data, bytes, s.delivered / 2)) return false;
        s.delivered += bytes;
        return true;
    };
    int code = lr2021_iq_stream_session(&h, &session, sink, &context, &native);
    result.validated_bytes = context.delivered;
    result.elapsed_us = native.elapsed_us;
    result.max_service_gap_us = native.max_service_gap_us;
    result.nominal_rate_hz = native.nominal_rate_hz;
    result.max_fifo_level = native.max_fifo_level;
    result.fifo_flags = native.fifo_flags;
    result.cancelled = native.cancelled;
    result.sink_rejected = native.stopped_by_sink && !native.cancelled;
    result.capture_error = native.capture_error;
    result.recovery_error = native.recovery_error;
    if (!code && result.sink_rejected) code = result.capture_error = LR2021_IQ_DATA;
    return code;
}
} // namespace platform::shared::radio
