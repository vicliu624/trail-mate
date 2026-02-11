#pragma once

#include <cstdint>

#include "sstv_config.h"
#include "sstv_dsp.h"

enum class Tone : uint8_t
{
    None = 0,
    Tone1100,
    Tone1200,
    Tone1300,
    Tone1900,
};

enum class HeaderState : uint8_t
{
    SeekLeader1 = 0,
    SeekBreak,
    SeekLeader2,
    SeekVisStart,
    ReadVisBits,
};

struct HeaderResult
{
    bool hop_ready = false;
    bool vis_start = false;
    bool in_vis_bits = false;
    float p1100 = 0.0f;
    float p1200 = 0.0f;
    float p1300 = 0.0f;
    float p1900 = 0.0f;
    Tone tone = Tone::None;
};

struct HeaderDetector
{
    int16_t header_buf[kHeaderWindowSamples] = {0};
    int16_t header_window[kHeaderWindowSamples] = {0};
    int header_pos = 0;
    int header_fill = 0;
    int header_hop = 0;

    int16_t visstart_buf[kVisStartWindowSamples] = {0};
    int visstart_pos = 0;
    int visstart_fill = 0;

    HeaderState state = HeaderState::SeekLeader1;
    int header_count = 0;
    int vis_start_count = 0;
    int vis_start_samples = 0;

    int header_log_tick = 0;
    int header_log_every = 10;
    int header_stat_tick = 0;
    int header_stat_every = 50;

    int break_window_count = 0;
    int break_hit_count = 0;
    double break_ratio_total_sum = 0.0;
    double break_ratio_max_sum = 0.0;
    int leader2_window_count = 0;
    int leader2_hit_count = 0;
    double leader2_ratio_total_sum = 0.0;
    double leader2_ratio_max_sum = 0.0;
    int vis_stat_window_count = 0;
    int vis_hit_count = 0;
    double vis_ratio_total_sum = 0.0;
    double vis_ratio_max_sum = 0.0;

    float header_window_ms = 0.0f;
    int leader_windows = 0;
    int break_windows = 0;

    GoertzelBin bin_1100 = {};
    GoertzelBin bin_1200 = {};
    GoertzelBin bin_1300 = {};
    GoertzelBin bin_1900 = {};
};

void header_detector_init(HeaderDetector& detector);
void header_detector_reset(HeaderDetector& detector);
bool header_detector_push_sample(HeaderDetector& detector, int16_t mono, HeaderResult& out);
const char* header_state_name(HeaderState state);
const char* tone_name(Tone tone);
