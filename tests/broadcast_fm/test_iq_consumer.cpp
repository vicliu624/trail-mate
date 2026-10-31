#include "platform/shared/radio/fm_iq_consumer.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using platform::shared::radio::BroadcastFmDecoder;
using platform::shared::radio::FmIqConsumer;
using Status = FmIqConsumer::Status;

int main()
{
    // Include signed extrema and values whose byte order differs visibly.
    constexpr int16_t levels[] = {-32768, -12345, 0, 12345, 32767};
    std::array<uint8_t, FmIqConsumer::kMaxBlockBytes> wire{};
    for (size_t n = 0; n < wire.size() / 2; ++n)
    {
        const uint16_t value = static_cast<uint16_t>(levels[n % 5]);
        wire[2 * n] = value & 255;
        wire[2 * n + 1] = value >> 8;
    }
    FmIqConsumer consumer;
    BroadcastFmDecoder reference;
    std::array<int16_t, FmIqConsumer::kMaxPcmSamples + 1> output{};
    std::vector<int16_t> expected;
    std::vector<int16_t> actual;
    for (size_t block = 0; block < 100; ++block)
    {
        output.back() = 12345;
        const auto result = consumer.consume(wire.data(), wire.size(), 250000, 7,
                                             block * 320, output.data(), output.size() - 1);
        assert(result.status == (block == 0 ? Status::GapReset : Status::Accepted));
        assert(result.samples <= FmIqConsumer::kMaxPcmSamples);
        assert(output.back() == 12345);
        actual.insert(actual.end(), output.begin(), output.begin() + result.samples);
        for (size_t n = 0; n < 320; ++n)
        {
            int16_t pcm;
            if (reference.push(levels[(2 * n) % 5], levels[(2 * n + 1) % 5], pcm)) expected.push_back(pcm);
        }
    }
    assert(expected == actual);
    assert(actual.size() == 6144);

    const auto fresh = [&](uint32_t generation, uint64_t index)
    {
        const auto result = consumer.consume(wire.data(), wire.size(), 250000, generation,
                                             index, output.data(), output.size() - 1);
        assert(result.status == Status::GapReset);
        assert(result.samples == 61);
        for (size_t n = 0; n < result.samples; ++n) assert(output[n] == expected[n]);
    };
    fresh(8, 32000); // Generation changes even with consecutive sample indices.
    fresh(8, 33000); // Forward gap.
    fresh(8, 0);     // Restart/backward indices.
    fresh(8, 0);     // Duplicate block.
    fresh(0xffffffff, 320);
    fresh(0, 640); // Generation wrap still resets.

    auto result = consumer.consume(wire.data(), 3, 250000, 0, 960, output.data(), output.size());
    assert(result.status == Status::InvalidBlock && result.samples == 0);
    fresh(0, 960);
    result = consumer.consume(nullptr, 4, 250000, 0, 1280, output.data(), output.size());
    assert(result.status == Status::InvalidBlock);
    result = consumer.consume(wire.data(), 0, 250000, 0, 1280, output.data(), output.size());
    assert(result.status == Status::InvalidBlock);
    result = consumer.consume(wire.data(), wire.size() + 4, 250000, 0, 1280, output.data(), output.size());
    assert(result.status == Status::InvalidBlock);
    result = consumer.consume(wire.data(), 4, 250000, 0, UINT64_MAX, output.data(), output.size());
    assert(result.status == Status::InvalidBlock);
    result = consumer.consume(wire.data(), wire.size(), 125000, 0, 1280, output.data(), output.size());
    assert(result.status == Status::UnsupportedRate);
    fresh(0, 1280);
    output.fill(12345);
    result = consumer.consume(wire.data(), wire.size(), 250000, 0, 1600, output.data(), 61);
    assert(result.status == Status::OutputFull && result.samples == 0);
    for (const auto sample : output) assert(sample == 12345); // Rejection is atomic.
    fresh(0, 1920);
    consumer.reset();
    fresh(0, 2240);
    std::puts("IQ consumer: signed LE16, exact PCM, bounded output, generation/gap/duplicate reset, malformed/rate/capacity rejection passed");
}
