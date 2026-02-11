#include "sstv_vis_decoder.h"

#include <cmath>
#include <cstring>

#include "sstv_config.h"
#include "sstv_log.h"

namespace
{
constexpr float kVisNoTiming = -1.0f;

struct FreqPeak
{
    float freq = 0.0f;
    float power = 0.0f;
};

struct VisConfig
{
    uint8_t vis;
    VisMode mode;
    float color_ms;
    float pd_scan_ms;
    float p_sync_ms;
    float p_porch_ms;
    float p_color_ms;
    int line_count;
};

constexpr VisConfig kVisConfigs[] = {
    {kVisScottie1, VisMode::Scottie1, kColorMsScottie1, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisScottie2, VisMode::Scottie2, kColorMsScottie2, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisScottieDX, VisMode::ScottieDX, kColorMsScottieDX, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisRobot72, VisMode::Robot72, kColorMsScottie1, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightRobot72},
    {kVisRobot36, VisMode::Robot36, kColorMsScottie1, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightRobot72},
    {kVisMartin1, VisMode::Martin1, kColorMsMartin1, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisMartin2, VisMode::Martin2, kColorMsMartin2, kVisNoTiming, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisPd50, VisMode::Pd50, kColorMsScottie1, kPd50ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisPd90, VisMode::Pd90, kColorMsScottie1, kPd90ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightScottie},
    {kVisPd120, VisMode::Pd120, kColorMsScottie1, kPd120ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightPd120},
    {kVisPd160, VisMode::Pd160, kColorMsScottie1, kPd160ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightPd160},
    {kVisPd180, VisMode::Pd180, kColorMsScottie1, kPd180ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightPd120},
    {kVisPd240, VisMode::Pd240, kColorMsScottie1, kPd240ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightPd120},
    {kVisPd290, VisMode::Pd290, kColorMsScottie1, kPd290ScanMs, kVisNoTiming, kVisNoTiming,
     kVisNoTiming, kInHeightPd290},
    {kVisP3, VisMode::P3, kColorMsScottie1, kVisNoTiming, kP3SyncMs, kP3PorchMs, kP3ColorMs,
     kInHeightPasokon},
    {kVisP5, VisMode::P5, kColorMsScottie1, kVisNoTiming, kP5SyncMs, kP5PorchMs, kP5ColorMs,
     kInHeightPasokon},
    {kVisP7, VisMode::P7, kColorMsScottie1, kVisNoTiming, kP7SyncMs, kP7PorchMs, kP7ColorMs,
     kInHeightPasokon},
};

uint8_t reverse_vis_bits(uint8_t value)
{
    uint8_t out = 0;
    for (int i = 0; i < 7; ++i)
    {
        out <<= 1;
        out |= static_cast<uint8_t>((value >> i) & 0x01);
    }
    return out;
}

const VisConfig* find_vis_config(uint8_t vis)
{
    for (const auto& cfg : kVisConfigs)
    {
        if (cfg.vis == vis)
        {
            return &cfg;
        }
    }
    return nullptr;
}

void fill_info(VisModeInfo& info, const VisConfig* config)
{
    if (!config)
    {
        info = VisModeInfo{};
        return;
    }
    info.mode = config->mode;
    info.color_ms = config->color_ms;
    info.pd_scan_ms = config->pd_scan_ms;
    info.p_sync_ms = config->p_sync_ms;
    info.p_porch_ms = config->p_porch_ms;
    info.p_color_ms = config->p_color_ms;
    info.line_count = config->line_count;
}

FreqPeak scan_peak(const int16_t* data, int count, float center, float span, float step)
{
    FreqPeak best;
    best.freq = center;
    best.power = 0.0f;
    float start = center - span;
    float end = center + span;
    for (float f = start; f <= end; f += step)
    {
        GoertzelBin bin = make_bin(f);
        float p = goertzel_power(data, count, bin);
        if (p > best.power)
        {
            best.power = p;
            best.freq = f;
        }
    }
    return best;
}
} // namespace

void vis_decoder_init(VisDecoder& decoder, int windows_per_bit)
{
    decoder.windows_per_bit = windows_per_bit;
    decoder.hop_target = windows_per_bit * (kVisBits + 1);
    if (decoder.hop_target > kVisMaxHops)
    {
        decoder.hop_target = kVisMaxHops;
    }
    decoder.bin_1100 = make_bin(1100.0f);
    decoder.bin_1200 = make_bin(1200.0f);
    decoder.bin_1300 = make_bin(1300.0f);
    vis_decoder_reset(decoder);
}

void vis_decoder_reset(VisDecoder& decoder)
{
    decoder.hop_count = 0;
    memset(decoder.hop_1100, 0, sizeof(decoder.hop_1100));
    memset(decoder.hop_1300, 0, sizeof(decoder.hop_1300));
    decoder.raw_len = 0;
    decoder.raw_needed = 0;
    decoder.raw_start_guess = 0;
    decoder.raw_search_margin = 0;
    decoder.raw_collect = false;
}

bool vis_decoder_push_hop(VisDecoder& decoder, float p1100, float p1300, VisDecodeResult& result)
{
    result = VisDecodeResult{};
    if (decoder.hop_count < decoder.hop_target)
    {
        decoder.hop_1100[decoder.hop_count] = p1100;
        decoder.hop_1300[decoder.hop_count] = p1300;
        decoder.hop_count++;
    }

    if (decoder.hop_count < decoder.hop_target)
    {
        return false;
    }

    int best_offset = -1;
    int best_bit_offset = 0;
    uint8_t best_value = 0;
    bool best_parity = false;
    int best_parity_bit = 0;
    double best_valid_avg = -1.0;
    double best_valid_min = 0.0;
    double best_valid_max = 0.0;

    // Search phase and optional start-bit skip (bit_offset=1) to avoid misalignment.
    for (int bit_offset = 1; bit_offset <= 1; ++bit_offset)
    {
        for (int offset = 0; offset < decoder.windows_per_bit; ++offset)
        {
            if (offset + (bit_offset + kVisBits) * decoder.windows_per_bit > decoder.hop_count)
            {
                break;
            }
            uint8_t value = 0;
            int ones = 0;
            double valid_sum = 0.0;
            double valid_min = 1.0;
            double valid_max = 0.0;
            int parity_bit = 0;

            for (int bit = 0; bit < kVisBits; ++bit)
            {
                double sum1100 = 0.0;
                double sum1300 = 0.0;
                const int base = offset + (bit_offset + bit) * decoder.windows_per_bit;
                for (int w = 0; w < decoder.windows_per_bit; ++w)
                {
                    int idx = base + w;
                    sum1100 += decoder.hop_1100[idx];
                    sum1300 += decoder.hop_1300[idx];
                }
                double pair = sum1100 + sum1300;
                double max_pair = sum1100 > sum1300 ? sum1100 : sum1300;
                double ratio = pair > 0.0 ? max_pair / pair : 0.0;
                if (ratio < valid_min)
                {
                    valid_min = ratio;
                }
                if (ratio > valid_max)
                {
                    valid_max = ratio;
                }
                valid_sum += ratio;
                int bit_val = (sum1100 >= sum1300) ? 1 : 0; // 1100=1, 1300=0
                if (bit < 7)
                {
                    if (bit_val)
                    {
                        value |= static_cast<uint8_t>(1u << bit);
                        ones++;
                    }
                }
                else
                {
                    parity_bit = bit_val;
                }
            }

            bool parity_ok = ((ones + parity_bit) % 2) == 0;
            double valid_avg = valid_sum / static_cast<double>(kVisBits);
            bool better = false;
            if (parity_ok && !best_parity)
            {
                better = true;
            }
            else if (parity_ok == best_parity && valid_avg > best_valid_avg)
            {
                better = true;
            }
            else if (parity_ok == best_parity && valid_avg == best_valid_avg &&
                     valid_min > best_valid_min)
            {
                better = true;
            }
            if (better)
            {
                best_offset = offset;
                best_bit_offset = bit_offset;
                best_value = value;
                best_parity = parity_ok;
                best_parity_bit = parity_bit;
                best_valid_avg = valid_avg;
                best_valid_min = valid_min;
                best_valid_max = valid_max;
            }
        }
    }

    if (best_offset < 0)
    {
        vis_decoder_reset(decoder);
        result.done = true;
        return true;
    }

    int bits[kVisBits] = {0};
    for (int bit = 0; bit < kVisBits; ++bit)
    {
        double sum1100 = 0.0;
        double sum1300 = 0.0;
        const int base = best_offset + (best_bit_offset + bit) * decoder.windows_per_bit;
        for (int w = 0; w < decoder.windows_per_bit; ++w)
        {
            int idx = base + w;
            sum1100 += decoder.hop_1100[idx];
            sum1300 += decoder.hop_1300[idx];
        }
        bits[bit] = (sum1100 >= sum1300) ? 1 : 0; // 1100=1, 1300=0
    }

    char bit_str[8] = {0};
    for (int i = 0; i < 7; ++i)
    {
        bit_str[i] = static_cast<char>('0' + bits[i]);
    }
    bit_str[7] = '\0';

    const double valid_avg = best_valid_avg;
    const bool valid_ok =
        (valid_avg >= kVisAcceptAvgValid && best_valid_min >= kVisAcceptMinValid);
    const bool fallback_ok =
        (valid_avg >= kVisFallbackAvgValid && best_valid_min >= kVisFallbackMinValid);
    const bool relaxed_ok = (valid_avg >= 0.45 && best_valid_min >= 0.10);

    SSTV_LOG_VIS("[SSTV] VIS phase=%d bit_offset=%d valid(min/avg/max)=%.2f/%.2f/%.2f\n",
                 best_offset, best_bit_offset, best_valid_min, best_valid_avg, best_valid_max);
    SSTV_LOG_VIS("[SSTV] VIS bits LSB=%s parity=%d (1100=1,1300=0)\n",
                 bit_str, bits[7]);
    SSTV_LOG("[SSTV] VIS value=%u parity=%d\n",
             static_cast<unsigned>(best_value), best_parity ? 1 : 0);

    bool accepted = false;
    const int ones = __builtin_popcount(best_value);
    const int inv_bit = best_parity_bit ? 0 : 1;
    const int inv_ones = 7 - ones;
    const bool inv_parity_ok = ((inv_ones + inv_bit) % 2) == 0;
    const VisConfig* config = nullptr;

    if (best_parity && valid_ok)
    {
        config = find_vis_config(best_value);
        accepted = config != nullptr;
        result.label = "normal";
    }
    else if (best_parity && relaxed_ok)
    {
        config = find_vis_config(best_value);
        accepted = config != nullptr;
        result.label = "relaxed";
    }
    else if (!valid_ok)
    {
        SSTV_LOG("[SSTV] VIS reject: low confidence avg=%.2f min=%.2f\n",
                 valid_avg, best_valid_min);
    }

    if (!accepted)
    {
        uint8_t inv_value = static_cast<uint8_t>(~best_value) & 0x7F;
        if ((fallback_ok || relaxed_ok) && inv_parity_ok)
        {
            config = find_vis_config(inv_value);
            accepted = config != nullptr;
            result.label = "inv";
        }
    }

    if (!accepted && best_parity && (fallback_ok || relaxed_ok))
    {
        uint8_t rev_value = reverse_vis_bits(best_value);
        config = find_vis_config(rev_value);
        accepted = config != nullptr;
        result.label = "rev";
    }

    if (!accepted)
    {
        uint8_t rev_inv_value = reverse_vis_bits(static_cast<uint8_t>(~best_value) & 0x7F);
        if ((fallback_ok || relaxed_ok) && inv_parity_ok)
        {
            config = find_vis_config(rev_inv_value);
            accepted = config != nullptr;
            result.label = "rev+inv";
        }
    }

    if (!accepted)
    {
        SSTV_LOG_V("[SSTV] VIS unsupported value=%u\n", static_cast<unsigned>(best_value));
    }

    result.done = true;
    result.accepted = accepted;
    result.parity_ok = best_parity;
    result.value = best_value;
    result.phase_offset = best_offset;
    result.bit_offset = best_bit_offset;
    result.valid_min = best_valid_min;
    result.valid_avg = best_valid_avg;
    result.valid_max = best_valid_max;
    if (accepted)
    {
        fill_info(result.info, config);
    }
    else
    {
        result.info = VisModeInfo{};
    }

    vis_decoder_reset(decoder);
    return true;
}

void vis_decoder_start_raw(VisDecoder& decoder,
                           const int16_t* preroll,
                           int preroll_len,
                           int start_back_samples)
{
    vis_decoder_reset(decoder);
    decoder.raw_collect = true;
    if (!preroll || preroll_len <= 0)
    {
        decoder.raw_len = 0;
    }
    else
    {
        if (preroll_len > kVisRawSamples)
        {
            preroll_len = kVisRawSamples;
        }
        memcpy(decoder.raw, preroll, static_cast<size_t>(preroll_len) * sizeof(int16_t));
        decoder.raw_len = preroll_len;
    }
    decoder.raw_search_margin = kVisSearchMarginSamples;
    decoder.raw_start_guess = decoder.raw_len - start_back_samples;
    if (decoder.raw_start_guess < 0)
    {
        decoder.raw_start_guess = 0;
    }
    const int total_samples = kVisBitSamples * (kVisBits + 2);
    int search_end = decoder.raw_start_guess + decoder.raw_search_margin;
    decoder.raw_needed = search_end + total_samples;
    if (decoder.raw_needed > kVisRawSamples)
    {
        decoder.raw_needed = kVisRawSamples;
    }
}

static bool decode_raw(VisDecoder& decoder, VisDecodeResult& result)
{
    result = VisDecodeResult{};
    const int total_samples = kVisBitSamples * (kVisBits + 2);
    int search_start = decoder.raw_start_guess - decoder.raw_search_margin;
    int search_end = decoder.raw_start_guess + decoder.raw_search_margin;
    if (search_start < 0)
    {
        search_start = 0;
    }
    if (search_end + total_samples > decoder.raw_len)
    {
        search_end = decoder.raw_len - total_samples;
    }
    if (search_end < search_start)
    {
        result.done = true;
        return true;
    }

    struct RawCandidate
    {
        int start = -1;
        uint8_t value = 0;
        bool parity_ok = false;
        int parity_bit = 0;
        double valid_min = 0.0;
        double valid_avg = -1.0;
        double valid_max = 0.0;
        double start_ratio = 0.0;
        double stop_ratio = 0.0;
        int tier = -1;
        const VisConfig* config = nullptr;
    };

    const int coarse_step = 4;
    const int fine_step = 1;
    const int fine_span = 24;
    auto find_best = [&](float freq_offset,
                         int start_min,
                         int start_max,
                         int step,
                         RawCandidate& best) -> bool
    {
        best = RawCandidate{};
        GoertzelBin bin1100 = make_bin(1100.0f + freq_offset);
        GoertzelBin bin1200 = make_bin(1200.0f + freq_offset);
        GoertzelBin bin1300 = make_bin(1300.0f + freq_offset);
        for (int start = start_min; start <= start_max; start += step)
        {
            const int stop_start = start + kVisBitSamples * (kVisBits + 1);
            float p1200_start = goertzel_power(decoder.raw + start, kVisBitSamples, bin1200);
            float p1100_start = goertzel_power(decoder.raw + start, kVisBitSamples, bin1100);
            float p1300_start = goertzel_power(decoder.raw + start, kVisBitSamples, bin1300);
            float p1200_stop = goertzel_power(decoder.raw + stop_start, kVisBitSamples, bin1200);
            float p1100_stop = goertzel_power(decoder.raw + stop_start, kVisBitSamples, bin1100);
            float p1300_stop = goertzel_power(decoder.raw + stop_start, kVisBitSamples, bin1300);

            double start_total = p1100_start + p1200_start + p1300_start;
            double stop_total = p1100_stop + p1200_stop + p1300_stop;
            double start_ratio_total = start_total > 0.0 ? p1200_start / start_total : 0.0;
            double stop_ratio_total = stop_total > 0.0 ? p1200_stop / stop_total : 0.0;
            double start_other = p1100_start > p1300_start ? p1100_start : p1300_start;
            double stop_other = p1100_stop > p1300_stop ? p1100_stop : p1300_stop;
            double start_ratio_max = start_other > 0.0 ? p1200_start / start_other : 0.0;
            double stop_ratio_max = stop_other > 0.0 ? p1200_stop / stop_other : 0.0;

            if (start_ratio_total < kVisStartTotalRatio ||
                start_ratio_max < kVisStartHoldRatio ||
                stop_ratio_total < kVisStartTotalRatio ||
                stop_ratio_max < kVisStartHoldRatio)
            {
                continue;
            }

            uint8_t value = 0;
            int ones = 0;
            double valid_sum = 0.0;
            double valid_min = 1.0;
            double valid_max = 0.0;
            int parity_bit = 0;

            for (int bit = 0; bit < kVisBits; ++bit)
            {
                const int bit_start = start + kVisBitSamples * (1 + bit);
                float p1100 = goertzel_power(decoder.raw + bit_start, kVisBitSamples, bin1100);
                float p1300 = goertzel_power(decoder.raw + bit_start, kVisBitSamples, bin1300);
                double pair = p1100 + p1300;
                double max_pair = p1100 > p1300 ? p1100 : p1300;
                double ratio = pair > 0.0 ? max_pair / pair : 0.0;
                if (ratio < valid_min)
                {
                    valid_min = ratio;
                }
                if (ratio > valid_max)
                {
                    valid_max = ratio;
                }
                valid_sum += ratio;
                int bit_val = (p1100 >= p1300) ? 1 : 0;
                if (bit < 7)
                {
                    if (bit_val)
                    {
                        value |= static_cast<uint8_t>(1u << bit);
                        ones++;
                    }
                }
                else
                {
                    parity_bit = bit_val;
                }
            }

            bool parity_ok = ((ones + parity_bit) % 2) == 0;
            double valid_avg = valid_sum / static_cast<double>(kVisBits);
            const bool valid_ok = (valid_avg >= kVisAcceptAvgValid &&
                                   valid_min >= kVisAcceptMinValid);
            const VisConfig* config = nullptr;
            if (parity_ok)
            {
                config = find_vis_config(value);
            }
            const bool supported = config != nullptr;
            const int tier = supported ? (valid_ok ? 3 : 2) : (parity_ok ? 1 : 0);
            bool better = false;
            if (tier > best.tier)
            {
                better = true;
            }
            else if (tier == best.tier && valid_avg > best.valid_avg)
            {
                better = true;
            }
            else if (tier == best.tier && valid_avg == best.valid_avg &&
                     valid_min > best.valid_min)
            {
                better = true;
            }

            if (better)
            {
                best.start = start;
                best.value = value;
                best.parity_ok = parity_ok;
                best.parity_bit = parity_bit;
                best.valid_min = valid_min;
                best.valid_avg = valid_avg;
                best.valid_max = valid_max;
                best.start_ratio = start_ratio_max;
                best.stop_ratio = stop_ratio_max;
                best.tier = tier;
                best.config = config;
            }
        }
        return best.start >= 0;
    };

    RawCandidate base;
    if (!find_best(0.0f, search_start, search_end, coarse_step, base))
    {
        result.done = true;
        return true;
    }

    RawCandidate base_refined = base;
    int refine_start = base.start - fine_span;
    int refine_end = base.start + fine_span;
    if (refine_start < search_start)
    {
        refine_start = search_start;
    }
    if (refine_end > search_end)
    {
        refine_end = search_end;
    }
    RawCandidate refined;
    if (find_best(0.0f, refine_start, refine_end, fine_step, refined))
    {
        bool better = false;
        if (refined.tier > base_refined.tier)
        {
            better = true;
        }
        else if (refined.tier == base_refined.tier &&
                 refined.valid_avg > base_refined.valid_avg)
        {
            better = true;
        }
        else if (refined.tier == base_refined.tier &&
                 refined.valid_avg == base_refined.valid_avg &&
                 refined.valid_min > base_refined.valid_min)
        {
            better = true;
        }
        if (better)
        {
            base_refined = refined;
        }
    }

    const int base_start = base_refined.start;
    const int base_stop = base_refined.start + kVisBitSamples * (kVisBits + 1);
    const FreqPeak base_start_peak =
        scan_peak(decoder.raw + base_start, kVisBitSamples, 1200.0f, 80.0f, 5.0f);
    const FreqPeak base_stop_peak =
        scan_peak(decoder.raw + base_stop, kVisBitSamples, 1200.0f, 80.0f, 5.0f);
    float freq_offset = 0.0f;
    bool offset_ok = false;
    const float offset_start = base_start_peak.freq - 1200.0f;
    const float offset_stop = base_stop_peak.freq - 1200.0f;
    if (fabsf(offset_start - offset_stop) <= 40.0f)
    {
        freq_offset = 0.5f * (offset_start + offset_stop);
        offset_ok = true;
    }
    else
    {
        freq_offset = offset_start;
        offset_ok = true;
    }

    RawCandidate corrected = base_refined;
    bool use_offset = false;
    const bool base_accept = (base_refined.parity_ok &&
                              base_refined.config != nullptr &&
                              base_refined.valid_avg >= kVisAcceptAvgValid &&
                              base_refined.valid_min >= kVisAcceptMinValid);
    if (offset_ok && (!base_accept || fabsf(freq_offset) >= 8.0f))
    {
        RawCandidate corr_coarse;
        if (find_best(freq_offset, search_start, search_end, coarse_step, corr_coarse))
        {
            RawCandidate corr_best = corr_coarse;
            int corr_refine_start = corr_coarse.start - fine_span;
            int corr_refine_end = corr_coarse.start + fine_span;
            if (corr_refine_start < search_start)
            {
                corr_refine_start = search_start;
            }
            if (corr_refine_end > search_end)
            {
                corr_refine_end = search_end;
            }
            RawCandidate corr_refined;
            if (find_best(freq_offset, corr_refine_start, corr_refine_end, fine_step,
                          corr_refined))
            {
                bool better = false;
                if (corr_refined.tier > corr_best.tier)
                {
                    better = true;
                }
                else if (corr_refined.tier == corr_best.tier &&
                         corr_refined.valid_avg > corr_best.valid_avg)
                {
                    better = true;
                }
                else if (corr_refined.tier == corr_best.tier &&
                         corr_refined.valid_avg == corr_best.valid_avg &&
                         corr_refined.valid_min > corr_best.valid_min)
                {
                    better = true;
                }
                if (better)
                {
                    corr_best = corr_refined;
                }
            }

            bool better = false;
            if (corr_best.tier > base_refined.tier)
            {
                better = true;
            }
            else if (corr_best.tier == base_refined.tier &&
                     corr_best.valid_avg > base_refined.valid_avg)
            {
                better = true;
            }
            else if (corr_best.tier == base_refined.tier &&
                     corr_best.valid_avg == base_refined.valid_avg &&
                     corr_best.valid_min > base_refined.valid_min)
            {
                better = true;
            }
            if (better)
            {
                corrected = corr_best;
                use_offset = true;
            }
        }
    }

    const RawCandidate& best = use_offset ? corrected : base_refined;
    const bool valid_ok = (best.valid_avg >= kVisAcceptAvgValid &&
                           best.valid_min >= kVisAcceptMinValid);
    bool accepted = (best.parity_ok && valid_ok && best.config != nullptr);

    char bit_str[8] = {0};
    for (int i = 0; i < 7; ++i)
    {
        bit_str[i] = static_cast<char>('0' + ((best.value >> i) & 0x01));
    }
    bit_str[7] = '\0';

    SSTV_LOG_VIS("[SSTV] VIS raw start_ratio=%.2f stop_ratio=%.2f offset=%d\n",
                 best.start_ratio, best.stop_ratio,
                 best.start - decoder.raw_start_guess);
    SSTV_LOG_VIS("[SSTV] VIS raw valid(min/avg/max)=%.2f/%.2f/%.2f\n",
                 best.valid_min, best.valid_avg, best.valid_max);
    SSTV_LOG_VIS("[SSTV] VIS bits LSB=%s parity_bit=%d parity_ok=%d (1100=1,1300=0)\n",
                 bit_str, best.parity_bit, best.parity_ok ? 1 : 0);
    SSTV_LOG_VIS("[SSTV] VIS freq_offset=%.1fHz%s\n",
                 static_cast<double>(use_offset ? freq_offset : 0.0f),
                 use_offset ? " (apply)" : "");
    {
        const int start_index = best.start;
        const int stop_index = best.start + kVisBitSamples * (kVisBits + 1);
        const int data0_index = best.start + kVisBitSamples;
        const FreqPeak start_peak = scan_peak(decoder.raw + start_index,
                                              kVisBitSamples, 1200.0f, 80.0f, 5.0f);
        const FreqPeak stop_peak = scan_peak(decoder.raw + stop_index,
                                             kVisBitSamples, 1200.0f, 80.0f, 5.0f);
        const FreqPeak bit0_1100 = scan_peak(decoder.raw + data0_index,
                                             kVisBitSamples, 1100.0f, 80.0f, 5.0f);
        const FreqPeak bit0_1300 = scan_peak(decoder.raw + data0_index,
                                             kVisBitSamples, 1300.0f, 80.0f, 5.0f);
        const bool bit0_is_1100 = (bit0_1100.power >= bit0_1300.power);
        const float bit0_freq = bit0_is_1100 ? bit0_1100.freq : bit0_1300.freq;
        const float bit0_expect = bit0_is_1100 ? 1100.0f : 1300.0f;
        SSTV_LOG_VIS("[SSTV] VIS freqpeak start=%.1fHz(%.1f) stop=%.1fHz(%.1f) bit0=%.1fHz(%.1f)\n",
                     static_cast<double>(start_peak.freq),
                     static_cast<double>(start_peak.freq - 1200.0f),
                     static_cast<double>(stop_peak.freq),
                     static_cast<double>(stop_peak.freq - 1200.0f),
                     static_cast<double>(bit0_freq),
                     static_cast<double>(bit0_freq - bit0_expect));
    }

    result.done = true;
    result.accepted = accepted;
    result.parity_ok = best.parity_ok;
    result.value = best.value;
    result.valid_min = best.valid_min;
    result.valid_avg = best.valid_avg;
    result.valid_max = best.valid_max;
    result.label = use_offset ? "raw+off" : "raw";
    if (accepted && best.config)
    {
        fill_info(result.info, best.config);
    }
    return true;
}

bool vis_decoder_push_raw(VisDecoder& decoder, int16_t sample, VisDecodeResult& result)
{
    if (!decoder.raw_collect)
    {
        return false;
    }
    if (decoder.raw_len < kVisRawSamples)
    {
        decoder.raw[decoder.raw_len++] = sample;
    }
    if (decoder.raw_len < decoder.raw_needed)
    {
        return false;
    }
    decoder.raw_collect = false;
    return decode_raw(decoder, result);
}

bool vis_decoder_is_collecting(const VisDecoder& decoder)
{
    return decoder.raw_collect;
}
