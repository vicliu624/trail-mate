#pragma once

#include <cstdint>

#include "sstv_config.h"
#include "sstv_dsp.h"

struct SyncDetector
{
    int16_t sync_buf[kSyncWindowSamples] = {0};
    int16_t sync_window[kSyncWindowSamples] = {0};
    int sync_pos = 0;
    int sync_fill = 0;
    int sync_hop = 0;
    GoertzelBin bin_1100 = {};
    GoertzelBin bin_1200 = {};
    GoertzelBin bin_1300 = {};
};

void sync_detector_init(SyncDetector& detector);
void sync_detector_reset(SyncDetector& detector);
bool sync_detector_push_sample(SyncDetector& detector,
                               int16_t mono,
                               bool can_sync,
                               int64_t sample_index,
                               int64_t min_sync_gap,
                               int64_t& last_sync_index);
