#include "sstv_vis_decoder.h"

#include <cstring>

#include "sstv_config.h"
#include "sstv_log.h"

namespace
{
constexpr float kVisNoTiming = -1.0f;

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
} // namespace

void vis_decoder_init(VisDecoder& decoder, int windows_per_bit)
{
    decoder.windows_per_bit = windows_per_bit;
    decoder.hop_target = windows_per_bit * (kVisBits + 1);
    if (decoder.hop_target > kVisMaxHops)
    {
        decoder.hop_target = kVisMaxHops;
    }
    vis_decoder_reset(decoder);
}

void vis_decoder_reset(VisDecoder& decoder)
{
    decoder.hop_count = 0;
    memset(decoder.hop_1100, 0, sizeof(decoder.hop_1100));
    memset(decoder.hop_1300, 0, sizeof(decoder.hop_1300));
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
    for (int bit_offset = 0; bit_offset <= 1; ++bit_offset)
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
                int bit_val = (sum1300 >= sum1100) ? 1 : 0; // 1100=0, 1300=1
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

    const double valid_avg = best_valid_avg;
    const bool valid_ok =
        (valid_avg >= kVisAcceptAvgValid && best_valid_min >= kVisAcceptMinValid);
    const bool fallback_ok =
        (valid_avg >= kVisFallbackAvgValid && best_valid_min >= kVisFallbackMinValid);
    const bool relaxed_ok = (valid_avg >= 0.45 && best_valid_min >= 0.10);

    SSTV_LOG_VIS("[SSTV] VIS phase=%d bit_offset=%d valid(min/avg/max)=%.2f/%.2f/%.2f\n",
                 best_offset, best_bit_offset, best_valid_min, best_valid_avg, best_valid_max);
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
