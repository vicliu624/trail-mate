#pragma once

#include <cstdint>

struct GoertzelBin
{
    float freq = 0.0f;
    float coeff = 0.0f;
    float cos_w = 0.0f;
    float sin_w = 0.0f;
};

GoertzelBin make_bin(float freq);
float goertzel_power(const int16_t* data, int count, const GoertzelBin& bin);
float goertzel_power_ring(const int16_t* data, int count, int pos, const GoertzelBin& bin);
void goertzel_update(float& q1, float& q2, float sample, const GoertzelBin& bin);
float goertzel_power_state(float q1, float q2, const GoertzelBin& bin);
float estimate_freq_from_bins(const int16_t* data, int count, const GoertzelBin* bins, int bin_count);
