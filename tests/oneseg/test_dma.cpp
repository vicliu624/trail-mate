#include "oneseg_lr2021_dma.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace platform::linux_runtime;
struct Model : OneSegLr2021Rx::Port
{
    uint64_t time_ns = 0, delivered = 0, next_byte = 0;
    uint32_t base = 0, size = 0, level = 0, max_level = 0;
    uint32_t rate = 500000;
    uint32_t spi_hz = 8350000;
    unsigned writes = 0, resets = 0, restores = 0, transfers = 0;
    uint8_t flags = 0;
    bool fast = false, active = false, restored = false, reject = false, immediate = false;
    bool fail_setup = false, wrong_clock = false;
    unsigned pause_us = 0, byte_ns = 958, second_start_us = 0, second_finish_us = 0;
    unsigned completion_us = 0;
    uint64_t target_bytes = 20000;
    void advance(uint64_t ns)
    {
        const auto before = time_ns * rate * 2 / 1000000000;
        time_ns += ns;
        if (active)
        {
            level += static_cast<uint32_t>(time_ns * rate * 2 / 1000000000 - before);
            if (level > 1024)
            {
                flags |= 0x10;
                level = 1024;
            }
            max_level = std::max(max_level, level);
        }
    }
    bool exclusivelyOwned() const override { return true; }
    uint32_t spiClockHz() const override { return fast ? (wrong_clock ? 12345000 : spi_hz) : 1000000; }
    int write(uint16_t, const uint8_t*, size_t) override
    {
        ++writes;
        return fail_setup ? -77 : 0;
    }
    int read(uint16_t op, uint8_t* out, size_t count) override
    {
        assert(count == 2);
        out[0] = op == 0x0101 ? 1 : 0;
        out[1] = op == 0x0101 ? 24 : 0;
        return 0;
    }
    lr2021_iq_hal hal()
    {
        lr2021_iq_hal h{};
        h.context = this;
        h.lock = [](void*) {};
        h.unlock = [](void*) {};
        h.snapshot = [](void*, lr2021_iq_profile* p)
        { *p = {}; p->lr2021 = p->configured = p->exclusive_lora = p->receiving = true; p->fw_major = 1; p->fw_minor = 24; return 0; };
        h.fast = [](void* c, bool value)
        { static_cast<Model*>(c)->fast = value; return 0; };
        h.read32 = [](void* c, uint32_t a, uint32_t* out)
        { auto& m = *static_cast<Model*>(c); *out = a == 0xf30028 ? m.base : a == 0xf30030 ? m.size : 0; return 0; };
        h.write32 = [](void* c, uint32_t a, uint32_t value)
        { auto& m = *static_cast<Model*>(c); (a == 0xf30028 ? m.base : m.size) = value; return 0; };
        h.write = [](void* c, uint16_t op, const uint8_t* data, size_t count)
        {
            auto& m = *static_cast<Model*>(c);
            if (op == 0x0290) assert(count == 11 && data[0] == 17 && data[5] == 8 &&
                                     ((uint32_t(data[1]) << 24) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 8) | data[4]) == m.rate);
            if (op == 0x020c) m.active = true;
            if (op == 0x0114) m.flags = 0;
            return 0;
        };
        h.read = [](void* c, uint16_t op, uint8_t* data, size_t count)
        {
            auto& m = *static_cast<Model*>(c);
            assert(count == 2);
            m.advance(200000);
            const unsigned value = op == 0x011c ? m.level : unsigned(m.flags) << 8;
            data[0] = value >> 8;
            data[1] = value & 255;
            return 0;
        };
        h.clear_fifo = [](void* c)
        { auto& m = *static_cast<Model*>(c); m.level = 0; return 0; };
        h.read_fifo = [](void* c, uint8_t* data, size_t count)
        {
            auto& m = *static_cast<Model*>(c);
            assert(count >= 4 && count <= 16380 && !(count % 4) && m.active && m.fast);
            if (m.spi_hz == 16000000) assert(count <= 2044);
            const auto start = m.time_ns;
            if (m.transfers == 1) m.advance(m.second_start_us * 1000);
            m.advance(m.byte_ns * 2);
            for (size_t i = 0; i < count; ++i)
            {
                m.advance(m.byte_ns);
                if (!m.level) m.flags |= 0x20;
                else --m.level;
                data[i] = static_cast<uint8_t>(m.next_byte++);
            }
            if (++m.transfers == 1 && m.pause_us * 1000 > m.time_ns - start)
                m.advance(m.pause_us * 1000 - (m.time_ns - start));
            if (m.transfers == 2) m.advance(m.second_finish_us * 1000);
            m.advance(m.completion_us * 1000);
            return 0;
        };
        h.standby = [](void* c)
        { static_cast<Model*>(c)->active = false; return 0; };
        h.reset = [](void* c)
        { ++static_cast<Model*>(c)->resets; return 0; };
        h.restore = [](void* c, const lr2021_iq_profile* p)
        { auto& m = *static_cast<Model*>(c); assert(p->receiving && !m.fast && !m.active); ++m.restores; m.restored = true; return 0; };
        h.now_us = [](void* c)
        { return static_cast<Model*>(c)->time_ns / 1000; };
        h.sleep_us = [](void* c, unsigned us)
        { static_cast<Model*>(c)->advance(us * 1000); };
        return h;
    }
    OneSegIqRequest request(uint8_t* staging)
    {
        return {13, staging, 2048,
                [](void* c, const uint8_t* bytes, size_t count, uint64_t first)
                { auto& m = *static_cast<Model*>(c); assert(first * 2 == m.delivered); if (m.reject) return false;
              for (size_t i = 0; i < count; ++i) assert(bytes[i] == static_cast<uint8_t>(m.delivered + i));
              m.delivered += count; return true; },
                [](void* c)
                { auto& m = *static_cast<Model*>(c); return m.immediate || m.delivered >= m.target_bytes; },
                this};
    }
};
int main()
{
    uint8_t staging[2048]{}, dma_staging[32768]{};
    for (unsigned mode = 0; mode < 23; ++mode)
    {
        Model model;
        if (mode == 1) model.pause_us = 8500;
        if (mode == 2) model.pause_us = 20000;
        if (mode == 3) model.byte_ns = 400;
        if (mode == 4) model.reject = true;
        if (mode == 5) model.fail_setup = true;
        if (mode == 6) model.wrong_clock = true;
        if (mode == 7) model.pause_us = 9800;
        // A completion leaves just below 512 bytes, followed by flags/query
        // overhead and a delayed next transfer start. Catch up before publication.
        if (mode == 8)
        {
            model.pause_us = 9620;
            model.second_start_us = 250;
        }
        if (mode == 9)
        {
            model.pause_us = 9620;
            model.second_start_us = 250;
            model.second_finish_us = 500;
        }
        if (mode == 10 || mode == 11) model.rate = 450000;
        if (mode == 11)
        {
            model.second_start_us = 250;
            model.second_finish_us = 500;
        }
        if (mode >= 12)
        {
            model.spi_hz = 8700000;
            model.byte_ns = 920;
        }
        if (mode == 13) model.pause_us = 5000;
        if (mode == 14)
        {
            model.pause_us = 5000;
            model.second_start_us = 250;
            model.second_finish_us = 500;
        }
        if (mode == 15) model.byte_ns = 400;
        if (mode == 16)
        {
            model.pause_us = 4900;
            model.second_start_us = 300;
        }
        if (mode == 17)
        {
            model.completion_us = 100;
            model.target_bytes = 500000;
        }
        if (mode == 18)
        {
            model.rate = 450000;
            model.target_bytes = 500000;
        }
        if (mode >= 19)
        {
            model.spi_hz = 16000000;
            model.byte_ns = 520;
            model.target_bytes = 500000;
        }
        if (mode == 20) model.completion_us = 150;
        if (mode == 21)
        {
            model.pause_us = 700;
            model.second_start_us = 250;
            model.second_finish_us = 500;
        }
        if (mode == 22) model.byte_ns = 320;
        OneSegIqResult result;
        const int code = run_oneseg_lr2021_dma(model.hal(), model, model.request(staging), result, 5, model.rate, dma_staging, sizeof(dma_staging));
        assert(result.nominal_rate_hz == model.rate);
        if (mode < 2 || (mode >= 7 && mode != 15 && mode != 22)) assert(!code && result.cancelled && result.validated_bytes >= model.target_bytes && result.validated_bytes == model.delivered && !result.fifo_flags);
        if (mode == 7) assert(model.transfers == 2);
        if (mode == 9) assert(model.transfers >= 3);
        if (mode == 2) assert(code == LR2021_IQ_DATA && result.fifo_flags & 0x10 && !model.delivered);
        if (mode == 3) assert(code == LR2021_IQ_DATA && result.fifo_flags & 0x20 && !model.delivered);
        if (mode == 15) assert(code == LR2021_IQ_DATA && result.fifo_flags & 0x20 && !model.delivered);
        if (mode == 22) assert(code == LR2021_IQ_DATA && result.fifo_flags & 0x20 && !model.delivered);
        if (mode == 4) assert(code == LR2021_IQ_DATA && result.sink_rejected && !model.delivered);
        if (mode == 5) assert(code == -77 && !model.delivered);
        if (mode == 6) assert(code == LR2021_IQ_ARGUMENT && !model.delivered);
        assert(model.resets == 1 && model.restores == 1 && model.restored && !result.recovery_error);
        std::fprintf(stderr, "DMA model case=%u result=%d peak=%u flags=%u bytes=%llu\n", mode, code, model.max_level, result.fifo_flags, (unsigned long long)result.validated_bytes);
    }
    Model stopped;
    stopped.immediate = true;
    OneSegIqResult result;
    assert(!run_oneseg_lr2021_dma(stopped.hal(), stopped, stopped.request(staging), result, 5, 500000, dma_staging, sizeof(dma_staging)));
    assert(result.cancelled && !stopped.writes && !stopped.resets);
    assert(run_oneseg_lr2021_dma(stopped.hal(), stopped, stopped.request(staging), result, 5, 449999, dma_staging, sizeof(dma_staging)) == LR2021_IQ_ARGUMENT);
}
