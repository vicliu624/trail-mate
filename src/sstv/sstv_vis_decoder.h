#pragma once

#include <cstdint>

#include "sstv_config.h"
#include "sstv_dsp.h"
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
    int16_t raw[kVisRawSamples] = {0};
    int raw_len = 0;
    int raw_needed = 0;
    int raw_start_guess = 0;
    int raw_search_margin = 0;
    bool raw_collect = false;
    GoertzelBin bin_1100 = {};
    GoertzelBin bin_1200 = {};
    GoertzelBin bin_1300 = {};
};

void vis_decoder_init(VisDecoder& decoder, int windows_per_bit);
void vis_decoder_reset(VisDecoder& decoder);
bool vis_decoder_push_hop(VisDecoder& decoder, float p1100, float p1300, VisDecodeResult& result);
void vis_decoder_start_raw(VisDecoder& decoder,
                           const int16_t* preroll,
                           int preroll_len,
                           int start_back_samples);
bool vis_decoder_push_raw(VisDecoder& decoder, int16_t sample, VisDecodeResult& result);
bool vis_decoder_is_collecting(const VisDecoder& decoder);
