#include "oneseg_lr2021_stream.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

using namespace platform::linux_runtime;
struct Fake : OneSegLr2021Rx::Port
{
    lr2021_iq_profile profile{};
    unsigned locked = 0, reads = 0, setup_writes = 0, resets = 0, restores = 0, polls = 0;
    uint32_t base = 0, size = 0;
    uint32_t expected_rate = 576000;
    unsigned idle_waits = 0;
    uint64_t time = 0, delivered = 0;
    bool fast = false, active = true, reject = false;
    uint8_t flags = 0;
    int fail_setup = 0, reset_error = 0, restore_error = 0;
    uint16_t level = 64;
    Fake()
    {
        profile.lr2021 = profile.configured = profile.exclusive_lora = profile.receiving = true;
        profile.fw_major = 1;
        profile.fw_minor = 24;
        profile.frequency_hz = 915250000;
        profile.bandwidth_hz = 250000;
        profile.spreading_factor = 8;
    }
    bool exclusivelyOwned() const override { return locked == 1 && profile.exclusive_lora; }
    uint32_t spiClockHz() const override { return fast ? 16000000 : 1000000; }
    int read(uint16_t op, uint8_t* data, size_t n) override
    {
        assert(locked == 1 && n == 2);
        ++reads;
        if (op == 0x0101)
        {
            data[0] = 1;
            data[1] = 24;
        }
        else
        {
            assert(op == 0x0110);
            data[0] = data[1] = 0;
        }
        return 0;
    }
    int write(uint16_t, const uint8_t*, size_t) override
    {
        assert(fast && locked == 1);
        return ++setup_writes == unsigned(fail_setup) ? -77 : 0;
    }
    lr2021_iq_hal hal()
    {
        lr2021_iq_hal h{};
        h.context = this;
        h.lock = [](void* c)
        { ++static_cast<Fake*>(c)->locked; };
        h.unlock = [](void* c)
        { --static_cast<Fake*>(c)->locked; };
        h.snapshot = [](void* c, lr2021_iq_profile* p)
        { *p = static_cast<Fake*>(c)->profile; return 0; };
        h.read32 = [](void* c, uint32_t a, uint32_t* value)
        {
            auto& f = *static_cast<Fake*>(c);
            if (a == 0x800ff8) *value = 0;
            else if (a == 0xf30028) *value = f.base;
            else
            {
                assert(a == 0xf30030);
                *value = f.size;
            }
            return 0;
        };
        h.write32 = [](void* c, uint32_t a, uint32_t v)
        {
            auto& f = *static_cast<Fake*>(c);
            if (a == 0xf30028) f.base = v;
            else
            {
                assert(a == 0xf30030);
                f.size = v;
            }
            return 0;
        };
        h.read = [](void* c, uint16_t op, uint8_t* p, size_t n)
        {
            auto& f = *static_cast<Fake*>(c);
            assert(n == 2);
            if (op == 0x011c)
            {
                p[0] = f.level >> 8;
                p[1] = f.level & 255;
            }
            else
            {
                assert(op == 0x011b);
                p[0] = f.flags;
                p[1] = 0;
            }
            return 0;
        };
        h.write = [](void* c, uint16_t op, const uint8_t* p, size_t n)
        {
            auto& f = *static_cast<Fake*>(c);
            if (op == 0x0290)
            {
                assert(n == 11 && p[0] == 17 && p[5] == 8 && p[10] == 1);
                const uint32_t rate = uint32_t(p[1]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 8 | p[4];
                assert(rate == f.expected_rate);
            }
            else if (op == 0x020c)
            {
                assert(n == 3 && p[0] == 255);
                f.active = true;
            }
            else
            {
                assert(op == 0x0114 && n == 2);
            }
            return 0;
        };
        h.standby = [](void* c)
        { static_cast<Fake*>(c)->active = false; return 0; };
        h.receive = [](void*) -> int
        { assert(false); return -99; };
        h.reset = [](void* c)
        {
            auto& f = *static_cast<Fake*>(c);
            assert(!f.fast);
            ++f.resets;
            f.active = false;
            return f.reset_error;
        };
        h.restore = [](void* c, const lr2021_iq_profile* p)
        {
            auto& f = *static_cast<Fake*>(c);
            ++f.restores;
            assert(p->frequency_hz == 915250000 && p->route == 0 && p->bandwidth_hz == 250000);
            f.active = p->receiving;
            return f.restore_error;
        };
        h.fast = [](void* c, bool enabled)
        { static_cast<Fake*>(c)->fast = enabled; return 0; };
        h.clear_fifo = [](void*)
        { return 0; };
        h.read_fifo = [](void* c, uint8_t* data, size_t n)
        { assert(static_cast<Fake*>(c)->active); std::memset(data, 0x42, n); return 0; };
        h.now_us = [](void* c) -> uint64_t
        { return static_cast<Fake*>(c)->time += 50; };
        h.sleep_us = [](void* c, unsigned us)
        { assert(us == 100); ++static_cast<Fake*>(c)->idle_waits; };
        return h;
    }
    OneSegIqRequest request(uint8_t* staging)
    {
        OneSegIqRequest r;
        r.staging = staging;
        r.staging_bytes = 1280;
        r.context = this;
        r.cancelled = [](void* c)
        { return ++static_cast<Fake*>(c)->polls >= 20; };
        r.accept = [](void* c, const uint8_t* data, size_t n, uint64_t first)
        {
            auto& f = *static_cast<Fake*>(c);
            assert(f.active && !f.flags && data[0] == 0x42 && first == f.delivered / 2);
            if (f.reject) return false;
            f.delivered += n;
            return true;
        };
        return r;
    }
};
int main()
{
    uint8_t staging[1280]{};
    for (unsigned scenario = 0; scenario < 7; ++scenario)
    {
        Fake f;
        OneSegIqResult result;
        if (scenario == 1) f.level = 0;
        if (scenario == 2) f.flags = 0x10;
        if (scenario == 3) f.reject = true;
        if (scenario == 4) f.reset_error = -17;
        if (scenario == 5) f.restore_error = -18;
        if (scenario == 6) f.profile.exclusive_lora = false;
        auto request = f.request(staging);
        const int code = run_oneseg_lr2021_stream(f.hal(), f, request, result);
        if (scenario <= 1) assert(code == 0 && result.cancelled && f.active);
        if (scenario == 2 || scenario == 3) assert(code == LR2021_IQ_DATA);
        if (scenario == 3) assert(result.sink_rejected && !result.validated_bytes);
        if (scenario == 4) assert(code == -17 && result.recovery_error == -17 && !f.restores);
        if (scenario == 5) assert(code == -18 && result.recovery_error == -18);
        if (scenario == 6) assert(code == LR2021_IQ_STATE && !f.resets && !f.setup_writes);
        else assert(f.resets == 1);
        assert(!f.locked && !f.fast);
        assert(result.validated_bytes == f.delivered);
        if (scenario == 0) assert(result.validated_bytes == 1024 && result.nominal_rate_hz == 576000);
    }
    for (unsigned step = 1; step <= 8; ++step)
    {
        Fake f;
        f.fail_setup = step;
        OneSegIqResult result;
        assert(run_oneseg_lr2021_stream(f.hal(), f, f.request(staging), result) == -77);
        assert(f.resets == 1 && f.restores == 1 && f.active && !f.fast && !f.delivered);
    }
    Fake cancelled;
    for (const unsigned level : {64u, 800u})
    {
        uint8_t large[2048]{};
        Fake paced;
        paced.level = level;
        auto input = paced.request(large);
        input.staging_bytes = sizeof(large);
        OneSegIqResult measured;
        assert(!run_oneseg_lr2021_stream(paced.hal(), paced, input, measured));
        assert(measured.cancelled && !measured.recovery_error);
        assert((paced.idle_waits > 0) == (level < 256));
    }
    for (const uint32_t rate : {450000u, 500000u})
    {
        Fake rate500;
        rate500.expected_rate = rate;
        OneSegIqResult measured;
        assert(!run_oneseg_lr2021_stream(rate500.hal(), rate500, rate500.request(staging), measured, 5, rate));
        assert(measured.nominal_rate_hz == rate && measured.validated_bytes && !measured.recovery_error);
    }
    for (const uint8_t flags : {uint8_t{0}, uint8_t{0x10}, uint8_t{0x20}})
    {
        Fake predicted;
        predicted.level = 800;
        predicted.expected_rate = 500000;
        predicted.flags = flags;
        OneSegIqResult measured;
        const int code = run_oneseg_lr2021_stream(predicted.hal(), predicted, predicted.request(staging), measured, 5, 500000, 4);
        if (flags) assert(code == LR2021_IQ_DATA && !measured.validated_bytes && !predicted.delivered);
        else assert(!code && measured.cancelled && measured.validated_bytes);
        assert(!measured.recovery_error && predicted.restores == 1 && predicted.active && !predicted.fast);
    }
    auto request = cancelled.request(staging);
    request.cancelled = [](void*)
    { return true; };
    OneSegIqResult result;
    assert(run_oneseg_lr2021_stream(cancelled.hal(), cancelled, request, result) == 0);
    assert(result.cancelled && !cancelled.reads && !cancelled.setup_writes && !cancelled.resets && !cancelled.locked);
}
