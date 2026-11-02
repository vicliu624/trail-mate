#pragma once
#include "platform/linux/oneseg_ofdm_carriers.h"
#include "platform/linux/oneseg_ofdm_lab.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

namespace platform::linux_runtime
{
struct OneSegCarrierMetadata
{
    uint64_t sequence = 0, burst = 0;
    unsigned layout = 0, slot = 0, mode = 0, fft = 0, cp = 0, phase = 0, flags = 0, count = 0;
    double sample_rate = 0, equalizer_floor = 0;
    bool valid() const
    {
        if (!sequence || !burst || layout > 1 || slot > 3 || phase > 3 || flags > 3 || mode < 1 || mode > 3 || !cp || fft % cp ||
            !std::isfinite(equalizer_floor) || equalizer_floor < 0 || equalizer_floor > 1) return false;
        const auto g = OneSegCarrierGeometry::centerSegment(mode, fft / cp, fft == 576);
        const auto stride = layout == 1 ? 12u : 2048u >> (mode - 1);
        return g.valid() && fft == g.fft_length && count == g.active_carriers && std::isfinite(sample_rate) &&
               std::abs(sample_rate / g.sample_rate - 1) <= 1e-6 &&
               burst == (sequence - 1) / stride + 1 && slot == (sequence - 1) % stride;
    }
    bool same(const OneSegCarrierMetadata& b) const
    {
        return sequence == b.sequence && burst == b.burst && layout == b.layout && slot == b.slot && mode == b.mode &&
               fft == b.fft && cp == b.cp && phase == b.phase && flags == b.flags && count == b.count &&
               sample_rate == b.sample_rate && equalizer_floor == b.equalizer_floor;
    }
};
struct OneSegCarrierValue
{
    // Actual pre-equalizer I/Q, equalized I/Q, H I/Q, reference I/Q.
    // Native reference is a scrambling value, known transmitted data ONLY on SP.
    std::array<float, 8> values{};
    bool valid() const
    {
        for (auto v : values)
            if (!std::isfinite(v) || std::abs(v) > 1e9f) return false;
        return true;
    }
};
struct OneSegCarrierChunk
{
    OneSegCarrierMetadata metadata;
    unsigned first = 0, count = 0, stride = 16;
    std::array<OneSegCarrierValue, 64> values{};
    bool valid() const
    {
        if (!metadata.valid() || (stride != 16 && stride != 64) || !count || count > values.size() || first % stride || first >= metadata.count ||
            count != std::min(stride, metadata.count - first)) return false;
        for (unsigned i = 0; i < count; ++i)
            if (!values[i].valid()) return false;
        return true;
    }
};

inline bool oneseg_decode_carrier_payload(const char* cursor, size_t length, OneSegCarrierChunk& value)
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "Carrier wire requires IEEE754 float32");
    if (!value.count || value.count > value.values.size()) return false;
    const size_t bytes = value.count * 8 * 4;
    if (length != ((bytes + 2) / 3) * 4) return false;
    std::array<uint8_t, 64 * 8 * 4> decoded{};
    const auto sextet = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63
                                        : -1;
    };
    size_t offset = 0;
    for (size_t i = 0; i < length; i += 4)
    {
        const int a = sextet(cursor[i]), b = sextet(cursor[i + 1]), c = sextet(cursor[i + 2]), d = sextet(cursor[i + 3]);
        const unsigned remaining = unsigned(std::min<size_t>(3, bytes - offset));
        if (a < 0 || b < 0) return false;
        if (remaining == 1)
        {
            if (cursor[i + 2] != '=' || cursor[i + 3] != '=' || (b & 15)) return false;
        }
        else if (remaining == 2)
        {
            if (c < 0 || cursor[i + 3] != '=' || (c & 3)) return false;
        }
        else if (c < 0 || d < 0) return false;
        const uint32_t word = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c < 0 ? 0 : c) << 6) | uint32_t(d < 0 ? 0 : d);
        for (unsigned j = 0; j < remaining; ++j) decoded[offset++] = uint8_t(word >> (16 - j * 8));
    }
    for (unsigned i = 0; i < value.count; ++i)
        for (unsigned j = 0; j < 8; ++j)
        {
            const size_t p = (i * 8 + j) * 4;
            const uint32_t bits = uint32_t(decoded[p]) | (uint32_t(decoded[p + 1]) << 8) | (uint32_t(decoded[p + 2]) << 16) | (uint32_t(decoded[p + 3]) << 24);
            std::memcpy(&value.values[i].values[j], &bits, 4);
        }
    return offset == bytes;
}
inline bool oneseg_parse_carrier_chunk(const char* cursor, OneSegCarrierChunk& output, bool binary = false)
{
    OneSegCarrierChunk value;
    value.stride = binary ? 64 : 16;
    const char* end = cursor + std::strlen(cursor);
    const auto number = [&cursor, end](auto& result)
    {
        while (cursor != end && *cursor == ' ') ++cursor;
        const auto parsed = std::from_chars(cursor, end, result);
        if (parsed.ec != std::errc{} || parsed.ptr == cursor || (parsed.ptr != end && *parsed.ptr != ' ')) return false;
        cursor = parsed.ptr;
        return true;
    };
    auto& m = value.metadata;
    if (!number(m.layout) || !number(m.sequence) || !number(m.burst) || !number(m.slot) || !number(m.mode) ||
        !number(m.fft) || !number(m.cp) || !number(m.phase) || !number(m.flags) || !number(m.sample_rate) ||
        !number(m.equalizer_floor) || !number(m.count) || !number(value.first) || !number(value.count) ||
        value.count > value.values.size()) return false;
    if (binary)
    {
        while (cursor != end && *cursor == ' ') ++cursor;
        const char* payload = cursor;
        while (cursor != end && *cursor != ' ') ++cursor;
        if (!oneseg_decode_carrier_payload(payload, size_t(cursor - payload), value)) return false;
    }
    else
        for (unsigned i = 0; i < value.count; ++i)
            for (auto& component : value.values[i].values)
                if (!number(component)) return false;
    while (cursor != end && *cursor == ' ') ++cursor;
    if (cursor != end || !value.valid()) return false;
    output = value;
    return true;
}

struct OneSegCarrierSymbol
{
    OneSegCarrierMetadata metadata;
    std::array<OneSegCarrierValue, 432> values{};
};
struct OneSegCarrierBurst
{
    OneSegLabSource source = OneSegLabSource::Unavailable;
    uint64_t updated_ms = 0;
    std::array<OneSegCarrierSymbol, 4> symbols{};
    bool fresh(uint64_t now) const { return now >= updated_ms && now - updated_ms <= 1500; }
};

// Worker-owned assembly; readers see only immutable, complete four-symbol bursts.
class OneSegCarrierCollector
{
  public:
    enum class Result
    {
        Rejected,
        Partial,
        Burst
    };
    void reset()
    {
        received_ = mask_ = 0;
        last_sequence_ = burst_id_ = 0;
        latest_.reset();
    }
    Result accept(const OneSegCarrierChunk& chunk, OneSegLabSource source, uint64_t now)
    {
        const auto& m = chunk.metadata;
        const bool origin = m.layout == 0 ? source == OneSegLabSource::LiveReceiver || source == OneSegLabSource::RecordedIq
                                          : source == OneSegLabSource::Synthetic;
        if (!chunk.valid() || !origin || m.sequence <= last_sequence_)
        {
            received_ = 0;
            return Result::Rejected;
        }
        if (!chunk.first)
        {
            if (received_ && m.sequence <= pending_.metadata.sequence)
            {
                received_ = 0;
                return Result::Rejected;
            }
            received_ = 0;
            pending_.metadata = m;
            pending_source_ = source;
        }
        if (!pending_.metadata.same(m) || chunk.first != received_ || source != pending_source_)
        {
            received_ = 0;
            return Result::Rejected;
        }
        for (unsigned i = 0; i < chunk.count; ++i) pending_.values[received_ + i] = chunk.values[i];
        received_ += chunk.count;
        if (received_ != m.count) return Result::Partial;
        received_ = 0;
        last_sequence_ = m.sequence;
        if (m.burst != burst_id_ || source != burst_.source)
        {
            burst_id_ = m.burst;
            mask_ = 0;
            burst_.source = source;
        }
        // Every frame must describe the same geometry and channel experiment.
        if (mask_)
        {
            const auto& first = burst_.symbols[0].metadata;
            if (!(mask_ & 1) || first.mode != m.mode || first.fft != m.fft || first.cp != m.cp || first.layout != m.layout ||
                first.sample_rate != m.sample_rate || first.equalizer_floor != m.equalizer_floor)
            {
                mask_ = 0;
                return Result::Rejected;
            }
        }
        burst_.symbols[m.slot] = pending_;
        mask_ |= 1u << m.slot;
        if (mask_ != 15) return Result::Partial;
        burst_.updated_ms = now;
        latest_ = std::make_shared<const OneSegCarrierBurst>(burst_);
        mask_ = 0;
        return Result::Burst;
    }
    std::shared_ptr<const OneSegCarrierBurst> latest() const { return latest_; }

  private:
    OneSegCarrierSymbol pending_;
    OneSegLabSource pending_source_ = OneSegLabSource::Unavailable;
    OneSegCarrierBurst burst_;
    unsigned received_ = 0, mask_ = 0;
    uint64_t last_sequence_ = 0, burst_id_ = 0;
    std::shared_ptr<const OneSegCarrierBurst> latest_;
};
} // namespace platform::linux_runtime
