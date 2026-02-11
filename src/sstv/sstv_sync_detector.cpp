#include "sstv_sync_detector.h"

#include <cstring>

#include "sstv_config.h"
#include "sstv_log.h"

void sync_detector_init(SyncDetector& detector)
{
    detector.bin_1100 = make_bin(1100.0f);
    detector.bin_1200 = make_bin(1200.0f);
    detector.bin_1300 = make_bin(1300.0f);
    sync_detector_reset(detector);
}

void sync_detector_reset(SyncDetector& detector)
{
    detector.sync_pos = 0;
    detector.sync_fill = 0;
    detector.sync_hop = 0;
    memset(detector.sync_buf, 0, sizeof(detector.sync_buf));
    memset(detector.sync_window, 0, sizeof(detector.sync_window));
}

bool sync_detector_push_sample(SyncDetector& detector,
                               int16_t mono,
                               bool can_sync,
                               int64_t sample_index,
                               int64_t min_sync_gap,
                               int64_t& last_sync_index)
{
    detector.sync_buf[detector.sync_pos] = mono;
    detector.sync_pos++;
    if (detector.sync_pos >= kSyncWindowSamples)
    {
        detector.sync_pos = 0;
    }
    if (detector.sync_fill < kSyncWindowSamples)
    {
        detector.sync_fill++;
    }

    if (!can_sync || detector.sync_fill < kSyncWindowSamples)
    {
        return false;
    }

    detector.sync_hop++;
    if (detector.sync_hop < kSyncHopSamples)
    {
        return false;
    }
    detector.sync_hop = 0;

    for (int j = 0; j < kSyncWindowSamples; ++j)
    {
        int idx = detector.sync_pos + j;
        if (idx >= kSyncWindowSamples)
        {
            idx -= kSyncWindowSamples;
        }
        detector.sync_window[j] = detector.sync_buf[idx];
    }

    float p1100 = goertzel_power(detector.sync_window, kSyncWindowSamples, detector.bin_1100);
    float p1200 = goertzel_power(detector.sync_window, kSyncWindowSamples, detector.bin_1200);
    float p1300 = goertzel_power(detector.sync_window, kSyncWindowSamples, detector.bin_1300);
    float total = p1100 + p1200 + p1300;
    float other_max = p1100 > p1300 ? p1100 : p1300;
    bool sync_hit = (p1200 > other_max * kSyncToneDetectRatio &&
                     p1200 > total * kSyncToneTotalRatio);

    if (sync_hit && sample_index - last_sync_index > min_sync_gap)
    {
        last_sync_index = sample_index;
        return true;
    }

    return false;
}
