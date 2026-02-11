#include "sstv_dsp.h"

#include <cmath>

#include "sstv_config.h"

GoertzelBin make_bin(float freq)
{
    GoertzelBin bin;
    bin.freq = freq;
    float w = 2.0f * static_cast<float>(M_PI) * freq / static_cast<float>(kSampleRate);
    bin.cos_w = cosf(w);
    bin.sin_w = sinf(w);
    bin.coeff = 2.0f * bin.cos_w;
    return bin;
}

float goertzel_power(const int16_t* data, int count, const GoertzelBin& bin)
{
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        q0 = bin.coeff * q1 - q2 + static_cast<float>(data[i]);
        q2 = q1;
        q1 = q0;
    }
    float real = q1 - q2 * bin.cos_w;
    float imag = q2 * bin.sin_w;
    return real * real + imag * imag;
}

float goertzel_power_ring(const int16_t* data, int count, int pos, const GoertzelBin& bin)
{
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    int idx = pos;
    for (int i = 0; i < count; ++i)
    {
        if (idx >= count)
        {
            idx = 0;
        }
        q0 = bin.coeff * q1 - q2 + static_cast<float>(data[idx]);
        q2 = q1;
        q1 = q0;
        idx++;
    }
    float real = q1 - q2 * bin.cos_w;
    float imag = q2 * bin.sin_w;
    return real * real + imag * imag;
}

void goertzel_update(float& q1, float& q2, float sample, const GoertzelBin& bin)
{
    float q0 = bin.coeff * q1 - q2 + sample;
    q2 = q1;
    q1 = q0;
}

float goertzel_power_state(float q1, float q2, const GoertzelBin& bin)
{
    float real = q1 - q2 * bin.cos_w;
    float imag = q2 * bin.sin_w;
    return real * real + imag * imag;
}

float estimate_freq_from_bins(const int16_t* data, int count, const GoertzelBin* bins, int bin_count)
{
    if (!bins || bin_count <= 0)
    {
        return kFreqMin;
    }

    float max_val = 0.0f;
    int max_idx = 0;
    float mags[kPixelBinCount];
    for (int i = 0; i < bin_count; ++i)
    {
        mags[i] = goertzel_power(data, count, bins[i]);
        if (mags[i] > max_val)
        {
            max_val = mags[i];
            max_idx = i;
        }
    }

    int left = max_idx > 0 ? max_idx - 1 : max_idx;
    int right = max_idx + 1 < bin_count ? max_idx + 1 : max_idx;
    float y1 = mags[left];
    float y2 = mags[max_idx];
    float y3 = mags[right];
    float denom = y1 + y2 + y3;
    float peak = static_cast<float>(max_idx);
    if (denom > 0.0f)
    {
        peak = peak + (y3 - y1) / denom;
    }

    float freq = kFreqMin + peak * kPixelBinStep;
    if (freq < kFreqMin)
    {
        freq = kFreqMin;
    }
    if (freq > kFreqMax)
    {
        freq = kFreqMax;
    }
    return freq;
}
