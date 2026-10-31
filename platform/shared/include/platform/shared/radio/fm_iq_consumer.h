#pragma once

#include "platform/shared/radio/broadcast_fm_decoder.h"
#include <limits>

namespace platform::shared::radio
{
// Consumer-task boundary for checked IQ16/IQ8 blocks. The FIFO callback must only
// copy into its bounded queue; call this after taking a block from that queue.
class FmIqConsumer
{
  public:
    static constexpr size_t kMaxBlockBytes = 1280;
    static constexpr size_t kMaxPcmSamples =
        (kMaxBlockBytes / 2 * BroadcastFmDecoder::kOutputRate + BroadcastFmDecoder::kInputRate - 1) /
        BroadcastFmDecoder::kInputRate;
    enum class Format
    {
        Iq16Le,
        Iq8,
        GainTaggedIq6 // Explicitly unsupported; low bits are gain, not signal.
    };
    enum class Status
    {
        Accepted,
        GapReset,
        InvalidBlock,
        UnsupportedRate,
        OutputFull,
        UnsupportedFormat
    };
    struct Result
    {
        Status status;
        size_t samples;
    };

    explicit FmIqConsumer(BroadcastFmDecoder::Deemphasis emphasis = BroadcastFmDecoder::Deemphasis::Us50)
        : decoder_(emphasis) {}

    void reset()
    {
        decoder_.reset();
        connected_ = false;
    }

    Result consume(const uint8_t* iq, size_t bytes, uint32_t rate,
                   uint32_t generation, uint64_t first_pair,
                   int16_t* pcm, size_t capacity, Format format = Format::Iq16Le)
    {
        if (format != Format::Iq16Le && format != Format::Iq8)
        {
            reset();
            return {Status::UnsupportedFormat, 0};
        }
        const size_t pair_bytes = format == Format::Iq8 ? 2 : 4;
        if (!iq || !bytes || bytes > kMaxBlockBytes || bytes % pair_bytes ||
            first_pair > std::numeric_limits<uint64_t>::max() - bytes / pair_bytes)
        {
            reset();
            return {Status::InvalidBlock, 0};
        }
        if (rate != BroadcastFmDecoder::kInputRate)
        {
            reset();
            return {Status::UnsupportedRate, 0};
        }
        // Reserve the worst-case count before consuming anything. A rejected
        // block breaks continuity even if the next block's indices look valid.
        const size_t required = (bytes / pair_bytes * BroadcastFmDecoder::kOutputRate +
                                 BroadcastFmDecoder::kInputRate - 1) /
                                BroadcastFmDecoder::kInputRate;
        if (!pcm || capacity < required)
        {
            reset();
            return {Status::OutputFull, 0};
        }
        const bool gap = !connected_ || generation != generation_ || first_pair != next_pair_ || format != format_;
        if (gap) decoder_.reset();
        size_t written = 0;
        for (size_t offset = 0; offset < bytes; offset += pair_bytes)
        {
            int16_t value = 0;
            const int16_t i = format == Format::Iq8 ? signed8(iq[offset]) : signed16(iq + offset);
            const int16_t q = format == Format::Iq8 ? signed8(iq[offset + 1]) : signed16(iq + offset + 2);
            if (decoder_.push(i, q, value))
                pcm[written++] = value;
        }
        generation_ = generation;
        next_pair_ = first_pair + bytes / pair_bytes;
        format_ = format;
        connected_ = true;
        return {gap ? Status::GapReset : Status::Accepted, written};
    }

  private:
    static int16_t signed8(uint8_t byte)
    {
        const int32_t value = byte >= 128 ? static_cast<int32_t>(byte) - 256 : byte;
        // Multiplication is defined for negative samples; left shift is not.
        return static_cast<int16_t>(value * 256);
    }
    static int16_t signed16(const uint8_t* bytes)
    {
        const uint32_t word = bytes[0] | static_cast<uint32_t>(bytes[1]) << 8;
        const int32_t signed_word = static_cast<int32_t>(word);
        return static_cast<int16_t>(word >= 0x8000 ? signed_word - 0x10000 : signed_word);
    }
    BroadcastFmDecoder decoder_;
    uint64_t next_pair_ = 0;
    uint32_t generation_ = 0;
    bool connected_ = false;
    Format format_ = Format::Iq16Le;
};
} // namespace platform::shared::radio
