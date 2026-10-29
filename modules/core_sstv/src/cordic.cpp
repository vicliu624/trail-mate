//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2023
// filename: cordic.cpp
// description: convert rectangular to polar using cordic
// License: MIT
//

#include <cstdint>
#include <cstdio>
#include <math.h>
#include <stdlib.h>

const uint8_t cordic_iterations = 16;
// Exact integer values produced by the original atan/gain initialization.
// Immutable tables permit independent receivers to initialize concurrently.
const int16_t thetas[cordic_iterations + 1] = {
    8192, 4836, 2555, 1297, 651, 326, 163, 81, 41, 20, 10, 5, 3, 1, 1, 0, 0};
const int16_t half_pi = 16384;
const int16_t recip_gain = 19897;

void cordic_init()
{
    // Retained for existing embedded callers; no mutable process-global state.
}

void cordic_rectangular_to_polar(int16_t i, int16_t q, uint16_t& magnitude,
                                 int16_t& phase)
{
    int32_t temp_i;
    int32_t i_32 = i;
    int32_t q_32 = q;

    if (i_32 < 0)
    {
        // rotate by an initial +/- 90 degrees
        temp_i = i_32;
        if (q > 0.0f)
        {
            i_32 = q_32; // subtract 90 degrees
            q_32 = -temp_i;
            phase = -half_pi;
        }
        else
        {
            i_32 = -q_32; // add 90 degrees
            q_32 = temp_i;
            phase = half_pi;
        }
    }
    else
    {
        phase = 0.0;
    }

    // rotate using "1 + jK" factors
    for (uint8_t idx = 0; idx <= cordic_iterations; idx++)
    {
        temp_i = i_32;
        if (q_32 >= 0.0)
        {
            // phase is positive: do negative roation
            i_32 += q_32 >> idx;
            q_32 -= temp_i >> idx;
            phase -= thetas[idx];
        }
        else
        {
            // phase is negative: do positive rotation
            i_32 -= q_32 >> idx;
            q_32 += temp_i >> idx;
            phase += thetas[idx];
        }
    }

    magnitude = (i_32 * recip_gain) >> 14;
}
