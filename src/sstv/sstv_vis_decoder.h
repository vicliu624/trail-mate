#pragma once

#include <cstdint>

#include "sstv_config.h"
#include "sstv_types.h"

struct VisDecodeResult
{
    bool done = false;
    bool accepted = false;
    bool parity_ok = false;
    uint8_t value = 0;
    int phase_offset = 0;
    int bit_offset = 0;
    double valid_min = 0.0;
    double valid_avg = 0.0;
    double valid_max = 0.0;
    const char* label = nullptr;
    VisModeInfo info = {};
};

struct VisDecoder
{
    float hop_1100[kVisMaxHops] = {0.0f};
    float hop_1300[kVisMaxHops] = {0.0f};
    int hop_count = 0;
    int hop_target = 0;
    int windows_per_bit = 0;
};

void vis_decoder_init(VisDecoder& decoder, int windows_per_bit);
void vis_decoder_reset(VisDecoder& decoder);
bool vis_decoder_push_hop(VisDecoder& decoder, float p1100, float p1300, VisDecodeResult& result);
