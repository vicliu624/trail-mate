#include "platform/shared/radio/fm_iq_consumer.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using platform::shared::radio::FmIqConsumer;
using Format = FmIqConsumer::Format;
using Status = FmIqConsumer::Status;
static_assert(FmIqConsumer::kMaxPcmSamples == 123);

int main()
{
    constexpr int levels[] = {-128, -127, -1, 0, 1, 63, 127};
    std::array<uint8_t, 1280> iq8{};
    std::array<uint8_t, 2560> iq16{};
    for (size_t n = 0; n < iq8.size(); ++n)
    {
        const int value = levels[n % 7];
        iq8[n] = static_cast<uint8_t>(value);
        const auto scaled = static_cast<uint16_t>(value * 256);
        iq16[2 * n] = scaled & 255;
        iq16[2 * n + 1] = scaled >> 8;
    }
    FmIqConsumer byte_consumer, word_consumer;
    std::array<int16_t, 124> byte_pcm{}, word_pcm{};
    std::vector<int16_t> byte_output, word_output;
    for (size_t block = 0; block < 100; ++block)
    {
        byte_pcm.back() = 12345;
        const auto result = byte_consumer.consume(iq8.data(), iq8.size(), 250000, 1, block * 640,
                                                  byte_pcm.data(), 123, Format::Iq8);
        assert(result.status == (block == 0 ? Status::GapReset : Status::Accepted));
        assert(result.samples <= 123 && byte_pcm.back() == 12345);
        byte_output.insert(byte_output.end(), byte_pcm.begin(), byte_pcm.begin() + result.samples);
        for (unsigned half = 0; half < 2; ++half)
        {
            const auto words = word_consumer.consume(iq16.data() + half * 1280, 1280, 250000, 1,
                                                     block * 640 + half * 320, word_pcm.data(), 123);
            assert(words.status == (block == 0 && half == 0 ? Status::GapReset : Status::Accepted));
            word_output.insert(word_output.end(), word_pcm.begin(), word_pcm.begin() + words.samples);
        }
    }
    assert(byte_output.size() == 12288 && byte_output == word_output);

    FmIqConsumer changing, fresh8, fresh16;
    assert(changing.consume(iq16.data(), 1280, 250000, 7, 0, byte_pcm.data(), 123).status == Status::GapReset);
    auto changed = changing.consume(iq8.data(), 1280, 250000, 7, 320, byte_pcm.data(), 123, Format::Iq8);
    auto expected = fresh8.consume(iq8.data(), 1280, 250000, 7, 320, word_pcm.data(), 123, Format::Iq8);
    assert(changed.status == Status::GapReset && changed.samples == expected.samples);
    for (size_t n = 0; n < changed.samples; ++n) assert(byte_pcm[n] == word_pcm[n]);
    changed = changing.consume(iq16.data(), 1280, 250000, 7, 960, byte_pcm.data(), 123);
    expected = fresh16.consume(iq16.data(), 1280, 250000, 7, 960, word_pcm.data(), 123);
    assert(changed.status == Status::GapReset && changed.samples == expected.samples);
    for (size_t n = 0; n < changed.samples; ++n) assert(byte_pcm[n] == word_pcm[n]);

    auto rejected = changing.consume(iq8.data(), 1280, 250000, 7, 1280, byte_pcm.data(), 123, Format::GainTaggedIq6);
    assert(rejected.status == Status::UnsupportedFormat && rejected.samples == 0);
    assert(changing.consume(iq8.data(), 1280, 250000, 7, 1280, byte_pcm.data(), 123, Format::Iq8).status == Status::GapReset);
    rejected = changing.consume(iq8.data(), 1280, 250000, 7, 1920, byte_pcm.data(), 123, static_cast<Format>(99));
    assert(rejected.status == Status::UnsupportedFormat && !rejected.samples);
    rejected = changing.consume(iq8.data(), 1279, 250000, 7, 1920, byte_pcm.data(), 123, Format::Iq8);
    assert(rejected.status == Status::InvalidBlock && !rejected.samples);
    rejected = changing.consume(iq8.data(), 1280, 250000, 7, UINT64_MAX, byte_pcm.data(), 123, Format::Iq8);
    assert(rejected.status == Status::InvalidBlock && !rejected.samples);
    rejected = changing.consume(iq8.data(), 1280, 125000, 7, 1920, byte_pcm.data(), 123, Format::Iq8);
    assert(rejected.status == Status::UnsupportedRate && !rejected.samples);
    byte_pcm.fill(12345);
    rejected = changing.consume(iq8.data(), 1280, 250000, 7, 1920, byte_pcm.data(), 122, Format::Iq8);
    assert(rejected.status == Status::OutputFull && !rejected.samples);
    for (const auto sample : byte_pcm) assert(sample == 12345);
    changed = changing.consume(iq8.data(), 1280, 250000, 7, 2560, byte_pcm.data(), 123, Format::Iq8);
    assert(changed.status == Status::GapReset && changed.samples == 122);
    FmIqConsumer tone_consumer;
    std::vector<int16_t> tone_pcm;
    constexpr double pi = 3.14159265358979323846;
    double phase = 0;
    for (size_t first = 0; first < 50000;)
    {
        const size_t pairs = std::min<size_t>(640, 50000 - first);
        for (size_t n = 0; n < pairs; ++n)
        {
            phase = std::remainder(phase + 2 * pi * 75000 * std::sin(2 * pi * 1000 * (first + n) / 250000) / 250000, 2 * pi);
            // Upstream raw IQ8 sources may carry only six significant bits.
            iq8[2 * n] = static_cast<uint8_t>(static_cast<int>(std::lround(31 * std::cos(phase))));
            iq8[2 * n + 1] = static_cast<uint8_t>(static_cast<int>(std::lround(31 * std::sin(phase))));
        }
        const auto tone = tone_consumer.consume(iq8.data(), pairs * 2, 250000, 9, first, byte_pcm.data(), 123, Format::Iq8);
        assert(tone.status == (first == 0 ? Status::GapReset : Status::Accepted));
        tone_pcm.insert(tone_pcm.end(), byte_pcm.begin(), byte_pcm.begin() + tone.samples);
        first += pairs;
    }
    assert(tone_pcm.size() == 9600);
    double sine = 0, cosine = 0;
    for (size_t n = 2400; n < tone_pcm.size(); ++n)
    {
        const double angle = 2 * pi * 1000 * n / 48000;
        sine += tone_pcm[n] * std::sin(angle) / 32767;
        cosine += tone_pcm[n] * std::cos(angle) / 32767;
    }
    const double recovered = 2 * std::hypot(sine, cosine) / (tone_pcm.size() - 2400);
    const double expected_amplitude = 1 / std::sqrt(1 + std::pow(2 * pi * 1000 * 50e-6, 2));
    assert(std::abs(recovered - expected_amplitude) < 0.015);
    std::printf("IQ8 consumer: signed extrema, exact IQ16-equivalent PCM, pair timeline, format reset, bounded rejection and low-resolution full-deviation tone %.6f passed\n", recovered);
}
