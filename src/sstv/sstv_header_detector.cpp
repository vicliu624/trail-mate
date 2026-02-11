#include "sstv_header_detector.h"

#include <cstring>

#include "sstv_config.h"
#include "sstv_log.h"

namespace
{
Tone detect_tone(float p1100, float p1200, float p1300, float p1900)
{
    float total = p1100 + p1200 + p1300 + p1900;
    float max_val = p1100;
    Tone tone = Tone::Tone1100;
    if (p1200 > max_val)
    {
        max_val = p1200;
        tone = Tone::Tone1200;
    }
    if (p1300 > max_val)
    {
        max_val = p1300;
        tone = Tone::Tone1300;
    }
    if (p1900 > max_val)
    {
        max_val = p1900;
        tone = Tone::Tone1900;
    }

    float other_max = 0.0f;
    if (tone != Tone::Tone1100 && p1100 > other_max)
    {
        other_max = p1100;
    }
    if (tone != Tone::Tone1200 && p1200 > other_max)
    {
        other_max = p1200;
    }
    if (tone != Tone::Tone1300 && p1300 > other_max)
    {
        other_max = p1300;
    }
    if (tone != Tone::Tone1900 && p1900 > other_max)
    {
        other_max = p1900;
    }

    if (max_val > other_max * kHeaderToneDetectRatio && max_val > total * kHeaderToneTotalRatio)
    {
        return tone;
    }

    return Tone::None;
}
} // namespace

void header_detector_init(HeaderDetector& detector)
{
    detector.header_window_ms =
        1000.0f * static_cast<float>(kHeaderHopSamples) / static_cast<float>(kSampleRate);
    detector.leader_windows = static_cast<int>(kLeaderMs / detector.header_window_ms + 0.5f);
    if (detector.leader_windows < 1)
    {
        detector.leader_windows = 1;
    }
    detector.break_windows = static_cast<int>(kBreakMs / detector.header_window_ms + 0.5f);
    if (detector.break_windows < 1)
    {
        detector.break_windows = 1;
    }

    detector.bin_1100 = make_bin(1100.0f);
    detector.bin_1200 = make_bin(1200.0f);
    detector.bin_1300 = make_bin(1300.0f);
    detector.bin_1900 = make_bin(1900.0f);

    header_detector_reset(detector);
}

void header_detector_reset(HeaderDetector& detector)
{
    detector.state = HeaderState::SeekLeader1;
    detector.header_count = 0;
    detector.vis_start_count = 0;
    detector.vis_start_samples = 0;
    detector.header_log_tick = 0;
    detector.header_stat_tick = 0;

    detector.break_window_count = 0;
    detector.break_hit_count = 0;
    detector.break_ratio_total_sum = 0.0;
    detector.break_ratio_max_sum = 0.0;
    detector.leader2_window_count = 0;
    detector.leader2_hit_count = 0;
    detector.leader2_ratio_total_sum = 0.0;
    detector.leader2_ratio_max_sum = 0.0;
    detector.vis_stat_window_count = 0;
    detector.vis_hit_count = 0;
    detector.vis_ratio_total_sum = 0.0;
    detector.vis_ratio_max_sum = 0.0;

    detector.header_pos = 0;
    detector.header_fill = 0;
    detector.header_hop = 0;
    detector.visstart_pos = 0;
    detector.visstart_fill = 0;
    memset(detector.header_buf, 0, sizeof(detector.header_buf));
    memset(detector.header_window, 0, sizeof(detector.header_window));
    memset(detector.visstart_buf, 0, sizeof(detector.visstart_buf));
}

bool header_detector_push_sample(HeaderDetector& detector, int16_t mono, HeaderResult& out)
{
    out = HeaderResult{};

    detector.visstart_buf[detector.visstart_pos] = mono;
    detector.visstart_pos++;
    if (detector.visstart_pos >= kVisStartWindowSamples)
    {
        detector.visstart_pos = 0;
    }
    if (detector.visstart_fill < kVisStartWindowSamples)
    {
        detector.visstart_fill++;
    }

    detector.header_buf[detector.header_pos] = mono;
    detector.header_pos++;
    if (detector.header_pos >= kHeaderWindowSamples)
    {
        detector.header_pos = 0;
    }
    if (detector.header_fill < kHeaderWindowSamples)
    {
        detector.header_fill++;
    }

    if (detector.header_fill < kHeaderWindowSamples)
    {
        return false;
    }

    detector.header_hop++;
    if (detector.header_hop < kHeaderHopSamples)
    {
        return false;
    }
    detector.header_hop = 0;

    for (int j = 0; j < kHeaderWindowSamples; ++j)
    {
        int idx = detector.header_pos + j;
        if (idx >= kHeaderWindowSamples)
        {
            idx -= kHeaderWindowSamples;
        }
        detector.header_window[j] = detector.header_buf[idx];
    }

    float p1100 = goertzel_power(detector.header_window, kHeaderWindowSamples, detector.bin_1100);
    float p1200 = goertzel_power(detector.header_window, kHeaderWindowSamples, detector.bin_1200);
    float p1300 = goertzel_power(detector.header_window, kHeaderWindowSamples, detector.bin_1300);
    float p1900 = goertzel_power(detector.header_window, kHeaderWindowSamples, detector.bin_1900);
    Tone tone = detect_tone(p1100, p1200, p1300, p1900);

    out.hop_ready = true;
    out.p1100 = p1100;
    out.p1200 = p1200;
    out.p1300 = p1300;
    out.p1900 = p1900;
    out.tone = tone;
    out.in_vis_bits = detector.state == HeaderState::ReadVisBits;

    if (detector.state != HeaderState::ReadVisBits)
    {
        if ((detector.state == HeaderState::SeekBreak ||
             detector.state == HeaderState::SeekLeader2 ||
             detector.state == HeaderState::SeekVisStart) &&
            (detector.header_log_tick++ % detector.header_log_every == 0))
        {
            SSTV_LOG_V("[SSTV] hdr state=%s tone=%s p1100=%.0f p1200=%.0f p1300=%.0f p1900=%.0f\n",
                       header_state_name(detector.state), tone_name(tone),
                       static_cast<double>(p1100), static_cast<double>(p1200),
                       static_cast<double>(p1300), static_cast<double>(p1900));
        }

        switch (detector.state)
        {
        case HeaderState::SeekLeader1:
        {
            bool leader1_hit = (tone == Tone::Tone1900);
            float total = p1100 + p1200 + p1300 + p1900;
            float max_other = p1100;
            if (p1200 > max_other)
            {
                max_other = p1200;
            }
            if (p1300 > max_other)
            {
                max_other = p1300;
            }
            if (!leader1_hit)
            {
                leader1_hit = (total > 0.0f &&
                               p1900 > total * 0.02f &&
                               p1900 > max_other * 0.03f);
                if (leader1_hit)
                {
                    SSTV_LOG_V("[SSTV] leader1 fallback p1900=%.0f p1100=%.0f p1200=%.0f p1300=%.0f\n",
                               static_cast<double>(p1900),
                               static_cast<double>(p1100),
                               static_cast<double>(p1200),
                               static_cast<double>(p1300));
                }
            }
            if (leader1_hit)
            {
                detector.header_count++;
                if (detector.header_count >= detector.leader_windows)
                {
                    detector.state = HeaderState::SeekBreak;
                    detector.header_count = 0;
                    SSTV_LOG("[SSTV] header leader1 ok (%.1fms@1900, expect %.0fms)\n",
                             static_cast<double>(detector.leader_windows) * detector.header_window_ms,
                             static_cast<double>(kLeaderMs));
                }
            }
            else
            {
                detector.header_count = 0;
            }
            break;
        }
        case HeaderState::SeekBreak:
        {
            bool break_hit = (tone == Tone::Tone1200);
            float total = p1100 + p1200 + p1300 + p1900;
            float max_other = p1300 > p1900 ? p1300 : p1900;
            float ratio_total = total > 0.0f ? p1200 / total : 0.0f;
            float ratio_max = max_other > 0.0f ? p1200 / max_other : 0.0f;
            detector.break_window_count++;
            detector.break_ratio_total_sum += ratio_total;
            detector.break_ratio_max_sum += ratio_max;
            if (!break_hit)
            {
                break_hit = (total > 0.0f &&
                             p1200 > total * 0.0005f &&
                             p1200 > max_other * 0.001f);
                if (break_hit)
                {
                    SSTV_LOG_V("[SSTV] break fallback p1200=%.0f p1300=%.0f p1900=%.0f\n",
                               static_cast<double>(p1200),
                               static_cast<double>(p1300),
                               static_cast<double>(p1900));
                }
                else if (detector.header_log_tick % detector.header_log_every == 0)
                {
                    SSTV_LOG_V("[SSTV] break miss r_total=%.3f r_max=%.3f\n",
                               static_cast<double>(ratio_total),
                               static_cast<double>(ratio_max));
                }
            }
            if (break_hit)
            {
                detector.break_hit_count++;
                detector.header_count++;
                if (detector.header_count >= detector.break_windows)
                {
                    detector.state = HeaderState::SeekLeader2;
                    detector.header_count = 0;
                    SSTV_LOG("[SSTV] header break ok (%.1fms@1200, expect %.0fms)\n",
                             static_cast<double>(detector.break_windows) * detector.header_window_ms,
                             static_cast<double>(kBreakMs));
                }
            }
            else if (tone == Tone::Tone1900)
            {
                detector.state = HeaderState::SeekLeader1;
                detector.header_count = 1;
            }
            else
            {
                detector.header_count = 0;
            }
            break;
        }
        case HeaderState::SeekLeader2:
        {
            float total = p1100 + p1200 + p1300 + p1900;
            float max_other = p1100;
            if (p1200 > max_other)
            {
                max_other = p1200;
            }
            if (p1300 > max_other)
            {
                max_other = p1300;
            }
            float ratio_total = total > 0.0f ? p1900 / total : 0.0f;
            float ratio_max = max_other > 0.0f ? p1900 / max_other : 0.0f;
            detector.leader2_window_count++;
            detector.leader2_ratio_total_sum += ratio_total;
            detector.leader2_ratio_max_sum += ratio_max;
            bool leader2_hit = (tone == Tone::Tone1900);
            if (!leader2_hit)
            {
                leader2_hit = (total > 0.0f &&
                               p1900 > total * 0.0005f &&
                               p1900 > max_other * 0.001f);
                if (leader2_hit)
                {
                    SSTV_LOG_V("[SSTV] leader2 fallback p1900=%.0f p1200=%.0f p1300=%.0f\n",
                               static_cast<double>(p1900),
                               static_cast<double>(p1200),
                               static_cast<double>(p1300));
                }
            }
            if (leader2_hit)
            {
                detector.header_count++;
                detector.leader2_hit_count++;
                if (detector.header_count >= detector.leader_windows)
                {
                    detector.state = HeaderState::SeekVisStart;
                    detector.header_count = 0;
                    detector.vis_start_count = 0;
                    SSTV_LOG("[SSTV] header leader2 ok (%.1fms@1900, expect %.0fms)\n",
                             static_cast<double>(detector.leader_windows) * detector.header_window_ms,
                             static_cast<double>(kLeaderMs));
                }
            }
            else
            {
                detector.state = HeaderState::SeekLeader1;
                detector.header_count = 0;
            }
            if (!leader2_hit && detector.header_log_tick % detector.header_log_every == 0)
            {
                SSTV_LOG_V("[SSTV] leader2 miss r_total=%.3f r_max=%.3f\n",
                           static_cast<double>(ratio_total),
                           static_cast<double>(ratio_max));
            }
            break;
        }
        case HeaderState::SeekVisStart:
        {
            bool vis_start = false;
            float ratio_total = 0.0f;
            float ratio_max = 0.0f;
            if (detector.visstart_fill >= kVisStartWindowSamples)
            {
                float v1100 = goertzel_power_ring(detector.visstart_buf,
                                                  kVisStartWindowSamples,
                                                  detector.visstart_pos, detector.bin_1100);
                float v1200 = goertzel_power_ring(detector.visstart_buf,
                                                  kVisStartWindowSamples,
                                                  detector.visstart_pos, detector.bin_1200);
                float v1300 = goertzel_power_ring(detector.visstart_buf,
                                                  kVisStartWindowSamples,
                                                  detector.visstart_pos, detector.bin_1300);
                float v1900 = goertzel_power_ring(detector.visstart_buf,
                                                  kVisStartWindowSamples,
                                                  detector.visstart_pos, detector.bin_1900);
                float total = v1100 + v1200 + v1300 + v1900;
                float max_other = v1300 > v1900 ? v1300 : v1900;
                if (v1100 > max_other)
                {
                    max_other = v1100;
                }
                ratio_total = total > 0.0f ? v1200 / total : 0.0f;
                ratio_max = max_other > 0.0f ? v1200 / max_other : 0.0f;
                bool v1200_max = (v1200 >= v1100 && v1200 >= v1300 && v1200 >= v1900);
                vis_start = (v1200_max &&
                             ratio_total >= kVisStartTotalRatio &&
                             ratio_max >= kVisStartHoldRatio);
            }
            detector.vis_stat_window_count++;
            detector.vis_ratio_total_sum += ratio_total;
            detector.vis_ratio_max_sum += ratio_max;
            if (!vis_start && detector.header_log_tick % detector.header_log_every == 0)
            {
                SSTV_LOG_V("[SSTV] visstart miss r_total=%.3f r_max=%.3f\n",
                           static_cast<double>(ratio_total),
                           static_cast<double>(ratio_max));
            }
            if (vis_start)
            {
                detector.vis_hit_count++;
                detector.state = HeaderState::ReadVisBits;
                detector.vis_start_count = 0;
                detector.vis_start_samples = 0;
                SSTV_LOG("[SSTV] header VIS start (>=%.1fms@1200, expect %.0fms)\n",
                         static_cast<double>(kVisStartHoldSamples) * 1000.0 / kSampleRate,
                         static_cast<double>(kVisBitMs));
                out.vis_start = true;
                out.in_vis_bits = true;
            }
            else
            {
                detector.vis_start_count = 0;
                detector.vis_start_samples = 0;
                if (detector.vis_stat_window_count >
                    static_cast<int>(detector.leader_windows * 2))
                {
                    detector.state = HeaderState::SeekLeader1;
                    detector.header_count = 0;
                    detector.vis_stat_window_count = 0;
                }
            }
            break;
        }
        case HeaderState::ReadVisBits:
            break;
        }

        if (detector.header_stat_tick++ % detector.header_stat_every == 0)
        {
            double break_avg_total =
                detector.break_window_count > 0
                    ? detector.break_ratio_total_sum / detector.break_window_count
                    : 0.0;
            double break_avg_max =
                detector.break_window_count > 0
                    ? detector.break_ratio_max_sum / detector.break_window_count
                    : 0.0;
            double leader2_avg_total = detector.leader2_window_count > 0
                                           ? detector.leader2_ratio_total_sum /
                                                 detector.leader2_window_count
                                           : 0.0;
            double leader2_avg_max = detector.leader2_window_count > 0
                                         ? detector.leader2_ratio_max_sum /
                                               detector.leader2_window_count
                                         : 0.0;
            double vis_avg_total = detector.vis_stat_window_count > 0
                                       ? detector.vis_ratio_total_sum /
                                             detector.vis_stat_window_count
                                       : 0.0;
            double vis_avg_max = detector.vis_stat_window_count > 0
                                     ? detector.vis_ratio_max_sum / detector.vis_stat_window_count
                                     : 0.0;
            SSTV_LOG_V("[SSTV] stat break=%d/%d avg(%.3f,%.3f) leader2=%d/%d avg(%.3f,%.3f) vis=%d/%d avg(%.3f,%.3f)\n",
                       detector.break_hit_count, detector.break_window_count, break_avg_total,
                       break_avg_max, detector.leader2_hit_count, detector.leader2_window_count,
                       leader2_avg_total, leader2_avg_max, detector.vis_hit_count,
                       detector.vis_stat_window_count, vis_avg_total, vis_avg_max);
        }
    }

    return true;
}

const char* header_state_name(HeaderState state)
{
    switch (state)
    {
    case HeaderState::SeekLeader1:
        return "Leader1";
    case HeaderState::SeekBreak:
        return "Break";
    case HeaderState::SeekLeader2:
        return "Leader2";
    case HeaderState::SeekVisStart:
        return "VisStart";
    case HeaderState::ReadVisBits:
        return "VisBits";
    default:
        return "Unknown";
    }
}

const char* tone_name(Tone tone)
{
    switch (tone)
    {
    case Tone::Tone1100:
        return "1100";
    case Tone::Tone1200:
        return "1200";
    case Tone::Tone1300:
        return "1300";
    case Tone::Tone1900:
        return "1900";
    default:
        return "None";
    }
}
