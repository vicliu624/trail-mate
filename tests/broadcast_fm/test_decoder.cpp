#include "platform/shared/radio/broadcast_fm_decoder.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using platform::shared::radio::BroadcastFmDecoder;
constexpr double pi = 3.14159265358979323846;

struct IQ
{
    int16_t i;
    int16_t q;
};

std::vector<IQ> tone(double frequency, double deviation, double offset = 0)
{
    std::vector<IQ> iq;
    double phase = 0;
    for (size_t n = 0; n < BroadcastFmDecoder::kInputRate; ++n)
    {
        const double sample = std::sin(2 * pi * frequency * n / BroadcastFmDecoder::kInputRate);
        phase = std::remainder(phase + 2 * pi * (offset + deviation * sample) / BroadcastFmDecoder::kInputRate, 2 * pi);
        iq.push_back({static_cast<int16_t>(std::lround(30000 * std::cos(phase))),
                      static_cast<int16_t>(std::lround(30000 * std::sin(phase)))});
    }
    return iq;
}

std::vector<int16_t> decode(BroadcastFmDecoder& decoder, const std::vector<IQ>& iq, size_t chunk)
{
    std::vector<int16_t> output;
    for (size_t begin = 0; begin < iq.size(); begin += chunk)
    {
        const size_t end = begin + chunk < iq.size() ? begin + chunk : iq.size();
        for (size_t n = begin; n < end; ++n)
        {
            int16_t pcm = 0;
            if (decoder.push(iq[n].i, iq[n].q, pcm)) output.push_back(pcm);
        }
    }
    return output;
}

double amplitude(const std::vector<int16_t>& pcm, double frequency)
{
    double sine = 0;
    double cosine = 0;
    const size_t start = 4800; // Discard startup and DC-block transients.
    for (size_t n = start; n < pcm.size(); ++n)
    {
        const double phase = 2 * pi * frequency * n / BroadcastFmDecoder::kOutputRate;
        sine += pcm[n] * std::sin(phase) / 32767;
        cosine += pcm[n] * std::cos(phase) / 32767;
    }
    return 2 * std::hypot(sine, cosine) / (pcm.size() - start);
}

int main()
{
    const auto iq = tone(1000, 20000, 4000);
    BroadcastFmDecoder decoder;
    const auto pcm = decode(decoder, iq, iq.size());
    assert(pcm.size() == BroadcastFmDecoder::kOutputRate);
    const double fundamental = amplitude(pcm, 1000);
    const double expected = (20000.0 / 75000) / std::sqrt(1 + std::pow(2 * pi * 1000 * 50e-6, 2));
    assert(std::abs(fundamental - expected) < 0.005);
    assert(amplitude(pcm, 2000) < 0.001);
    double mean = 0;
    for (size_t n = 24000; n < pcm.size(); ++n) mean += pcm[n];
    assert(std::abs(mean / 24000) < 2);

    decoder.reset();
    assert(decode(decoder, iq, 137) == pcm);
    decoder.reset();
    assert(decode(decoder, iq, 1) == pcm);

    BroadcastFmDecoder us75(BroadcastFmDecoder::Deemphasis::Us75);
    const double amplitude75 = amplitude(decode(us75, iq, 511), 1000);
    const double expected75 = (20000.0 / 75000) / std::sqrt(1 + std::pow(2 * pi * 1000 * 75e-6, 2));
    assert(std::abs(amplitude75 - expected75) < 0.005);
    assert(amplitude75 < fundamental);

    decoder.reset();
    const auto rejected = decode(decoder, tone(40000, 20000), 257);
    assert(amplitude(rejected, 8000) < fundamental * 0.01); // >40 dB alias rejection.

    decoder.reset(); // A lost IQ block/retune must discard all prior signal state.
    int16_t sample = 123;
    size_t silence = 0;
    for (size_t n = 0; n < BroadcastFmDecoder::kInputRate; ++n)
    {
        if (decoder.push(0, 0, sample))
        {
            assert(sample == 0);
            ++silence;
        }
    }
    assert(silence == BroadcastFmDecoder::kOutputRate);
    decoder.reset();
    assert(decode(decoder, iq, 113) == pcm); // Resume starts from a clean generation.
    decoder.reset();
    const auto full_deviation = decode(decoder, tone(1000, 75000), 193);
    assert(std::abs(amplitude(full_deviation, 1000) - expected * 3.75) < 0.01);
    std::printf("FM DSP: 250k IQ to 48k PCM, tone %.6f, 50/75us emphasis, DC rejection, alias rejection, chunk continuity and gap reset passed\n", fundamental);
}
