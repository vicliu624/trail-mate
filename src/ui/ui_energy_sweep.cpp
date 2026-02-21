#include "ui_energy_sweep.h"

#include "../app/app_context.h"
#include "../app/app_tasks.h"
#include "../board/LoraBoard.h"
#include "../chat/infra/meshcore/mc_region_presets.h"
#include "../chat/infra/meshtastic/mt_region.h"
#include "ui_common.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

// Forward declarations from main.cpp
extern void disableScreenSleep();
extern void enableScreenSleep();
extern lv_group_t* app_g;

namespace
{
constexpr int kScreenW = 480;
constexpr int kScreenH = 222;
constexpr int kTopBarH = 28;

constexpr int kLeftPanelX = 12;
constexpr int kLeftPanelY = 40;
constexpr int kLeftPanelW = 332;
constexpr int kLeftPanelH = 170;

constexpr int kPlotX = 10;
constexpr int kPlotY = 10;
constexpr int kPlotW = 312;
constexpr int kPlotH = 118;

constexpr int kScaleBarX = 10;
constexpr int kScaleBarY = 130;
constexpr int kScaleBarW = 312;
constexpr int kScaleBarH = 28;

constexpr int kRightPanelX = 354;
constexpr int kRightPanelY = 40;
constexpr int kRightPanelW = 114;
constexpr int kRightPanelH = 170;

constexpr float kDefaultFreqStartMhz = 433.050f;
constexpr float kDefaultFreqEndMhz = 434.790f;
constexpr float kStepQuantMhz = 0.025f;
constexpr int kTargetBinCount = 70;
constexpr int kMaxBins = 96;
constexpr int kScanIntervalMs = 35;
constexpr int kSampleSettleMs = 2;
constexpr int kSampleCount = 5;
constexpr int kSampleGapMs = 1;

constexpr float kRssiFloor = -125.0f;
constexpr float kRssiCeil = -80.0f;
constexpr float kNoiseEmaPrev = 0.7f;
constexpr float kNoiseEmaNew = 0.3f;
constexpr float kSweepEmaNew = 0.6f;
constexpr float kSweepEmaPrev = 0.4f;
constexpr float kHotEnterMarginDb = 10.0f;
constexpr float kHotExitMarginDb = 7.0f;

constexpr int kBestGuardBins = 2;

constexpr uint32_t kColorAmber = 0xEBA341;
constexpr uint32_t kColorAmberDark = 0xC98118;
constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorPanelBg = 0xFAF0D8;
constexpr uint32_t kColorLine = 0xE7C98F;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;
constexpr uint32_t kColorWarn = 0xB94A2C;
constexpr uint32_t kColorOk = 0x3E7D3E;
constexpr uint32_t kColorInfo = 0x2D6FB6;

struct EnergySweepUi
{
    lv_obj_t* root = nullptr;

    lv_obj_t* topbar = nullptr;
    lv_obj_t* back_btn = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* mode_chip = nullptr;
    lv_obj_t* mode_chip_label = nullptr;
    lv_obj_t* cad_chip = nullptr;
    lv_obj_t* cad_chip_label = nullptr;

    lv_obj_t* left_panel = nullptr;
    lv_obj_t* plot_area = nullptr;
    std::array<lv_obj_t*, kMaxBins> bars{};
    lv_obj_t* cursor_line = nullptr;
    lv_obj_t* cursor_tip = nullptr;
    lv_obj_t* scale_left = nullptr;
    lv_obj_t* scale_mid = nullptr;
    lv_obj_t* scale_right = nullptr;

    lv_obj_t* right_panel = nullptr;
    lv_obj_t* cursor_freq = nullptr;
    lv_obj_t* cursor_unit = nullptr;
    lv_obj_t* rssi_label = nullptr;
    lv_obj_t* noise_label = nullptr;
    lv_obj_t* best_freq = nullptr;
    lv_obj_t* best_snr = nullptr;
    lv_obj_t* progress_bar = nullptr;
    lv_obj_t* progress_pct = nullptr;
    lv_obj_t* btn_scan = nullptr;
    lv_obj_t* btn_scan_label = nullptr;
    lv_obj_t* btn_auto = nullptr;
    lv_obj_t* btn_auto_label = nullptr;
};

struct RadioContext
{
    LoraBoard* lora = nullptr;
    bool use_hw = false;
    bool paused_by_us = false;
    float bw_khz = 125.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    int8_t tx_power = 14;
    uint8_t preamble_len = 8;
    uint8_t sync_word = 0x12;
    uint8_t crc_len = 2;
};

struct SweepState
{
    bool scanning = true;
    bool auto_applied = false;
    int cursor_index = 0;
    int scan_index = 0;
    int scanned_bins = 0;
    int completed_cycles = 0;
    float progress = 0.0f;
    int best_index = 0;
    float noise_dbm = -104.0f;
    bool noise_valid = false;
    std::array<float, kMaxBins> rssi{};
    std::array<float, kMaxBins> smooth{};
    std::array<uint8_t, kMaxBins> hot{};
    uint32_t rand_state = 0xA5C34D29u;
    float sim_phase = 0.0f;
};

struct SweepBandPlan
{
    float freq_start_mhz = kDefaultFreqStartMhz;
    float freq_end_mhz = kDefaultFreqEndMhz;
    float step_mhz = kStepQuantMhz;
    float bw_khz = 125.0f;
    int bin_count = static_cast<int>(((kDefaultFreqEndMhz - kDefaultFreqStartMhz) / kStepQuantMhz) + 0.5f) + 1;
};

EnergySweepUi s_ui;
SweepState s_state;
RadioContext s_radio;
SweepBandPlan s_band;
lv_timer_t* s_refresh_timer = nullptr;

int active_bin_count()
{
    if (s_band.bin_count < 2)
    {
        return 2;
    }
    if (s_band.bin_count > kMaxBins)
    {
        return kMaxBins;
    }
    return s_band.bin_count;
}

int clamp_index(int idx)
{
    if (idx < 0)
    {
        return 0;
    }
    const int bins = active_bin_count();
    if (idx >= bins)
    {
        return bins - 1;
    }
    return idx;
}

float bin_to_freq_mhz(int idx)
{
    return s_band.freq_start_mhz + static_cast<float>(idx) * s_band.step_mhz;
}

float preset_to_bw_khz(uint8_t modem_preset, bool wide_lora)
{
    const auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(modem_preset);
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        return wide_lora ? 1625.0f : 500.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        return wide_lora ? 812.5f : 250.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
        return wide_lora ? 406.25f : 125.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        return wide_lora ? 812.5f : 250.0f;
    }
}

const chat::meshtastic::RegionInfo* find_region_for_frequency(float freq_mhz)
{
    size_t count = 0;
    const chat::meshtastic::RegionInfo* regions = chat::meshtastic::getRegionTable(&count);
    if (!regions || count == 0)
    {
        return nullptr;
    }

    const chat::meshtastic::RegionInfo* best = nullptr;
    float best_dist = 1e9f;

    for (size_t i = 0; i < count; ++i)
    {
        const chat::meshtastic::RegionInfo& region = regions[i];
        if (region.code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            continue;
        }

        float dist = 0.0f;
        if (freq_mhz < region.freq_start_mhz)
        {
            dist = region.freq_start_mhz - freq_mhz;
        }
        else if (freq_mhz > region.freq_end_mhz)
        {
            dist = freq_mhz - region.freq_end_mhz;
        }

        if (!best || dist < best_dist)
        {
            best = &region;
            best_dist = dist;
        }
    }

    return best ? best : chat::meshtastic::findRegion(meshtastic_Config_LoRaConfig_RegionCode_CN);
}

void apply_band_plan(float start_mhz, float end_mhz, float bw_khz)
{
    if (!std::isfinite(start_mhz) || !std::isfinite(end_mhz))
    {
        start_mhz = kDefaultFreqStartMhz;
        end_mhz = kDefaultFreqEndMhz;
    }
    if (end_mhz < start_mhz)
    {
        std::swap(start_mhz, end_mhz);
    }

    const float safe_bw_khz = (std::isfinite(bw_khz) && bw_khz > 1.0f) ? bw_khz : 125.0f;
    const float half_bw_mhz = safe_bw_khz / 2000.0f;
    if ((end_mhz - start_mhz) > (2.0f * half_bw_mhz))
    {
        start_mhz += half_bw_mhz;
        end_mhz -= half_bw_mhz;
    }

    const float span_mhz = std::max(kStepQuantMhz, end_mhz - start_mhz);
    float step_mhz = span_mhz / static_cast<float>(kTargetBinCount - 1);
    if (!std::isfinite(step_mhz) || step_mhz < kStepQuantMhz)
    {
        step_mhz = kStepQuantMhz;
    }
    step_mhz = std::ceil(step_mhz / kStepQuantMhz) * kStepQuantMhz;

    int bins = static_cast<int>(std::floor(span_mhz / step_mhz)) + 1;
    if (bins < 2)
    {
        bins = 2;
    }
    while (bins > kMaxBins)
    {
        step_mhz += kStepQuantMhz;
        bins = static_cast<int>(std::floor(span_mhz / step_mhz)) + 1;
    }

    s_band.freq_start_mhz = start_mhz;
    s_band.step_mhz = step_mhz;
    s_band.bin_count = bins;
    s_band.freq_end_mhz = start_mhz + step_mhz * static_cast<float>(bins - 1);
    s_band.bw_khz = safe_bw_khz;
}

void setup_sweep_band_plan()
{
    const app::AppConfig& cfg = app::AppContext::getInstance().getConfig();

    float start_mhz = kDefaultFreqStartMhz;
    float end_mhz = kDefaultFreqEndMhz;
    float bw_khz = 125.0f;

    if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        const chat::MeshConfig& mesh = cfg.meshtastic_config;
        auto region_code = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(mesh.region);
        if (region_code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
        }
        const chat::meshtastic::RegionInfo* region = chat::meshtastic::findRegion(region_code);
        if (region)
        {
            start_mhz = region->freq_start_mhz;
            end_mhz = region->freq_end_mhz;
            bw_khz = mesh.use_preset ? preset_to_bw_khz(mesh.modem_preset, region->wide_lora)
                                     : mesh.bandwidth_khz;
        }
    }
    else
    {
        const chat::MeshConfig& mesh = cfg.meshcore_config;
        float hint_freq_mhz = mesh.meshcore_freq_mhz;
        if (mesh.meshcore_region_preset > 0)
        {
            const chat::meshcore::RegionPreset* preset =
                chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset);
            if (preset)
            {
                hint_freq_mhz = preset->freq_mhz;
            }
        }

        const chat::meshtastic::RegionInfo* region = find_region_for_frequency(hint_freq_mhz);
        if (region)
        {
            start_mhz = region->freq_start_mhz;
            end_mhz = region->freq_end_mhz;
        }
        bw_khz = mesh.meshcore_bw_khz;
    }

    apply_band_plan(start_mhz, end_mhz, bw_khz);
}

uint32_t next_random()
{
    uint32_t x = s_state.rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_state.rand_state = x;
    return x;
}

float random_unit()
{
    return static_cast<float>(next_random() & 0xFFFFu) / 65535.0f;
}

float simulated_rssi_for_bin(int idx)
{
    const float t = s_state.sim_phase;
    float value = -111.0f + 3.2f * std::sin((idx + t) * 0.21f) + 2.4f * std::cos((idx + t) * 0.067f);
    value += (random_unit() - 0.5f) * 3.5f;

    if (idx >= 32 && idx <= 38)
    {
        value = -92.0f + (random_unit() - 0.5f) * 3.0f;
    }

    const int bins = active_bin_count();
    const int moving_peak = (bins > 0) ? (static_cast<int>(t) % bins) : 0;
    const int dist = std::abs(idx - moving_peak);
    if (dist <= 2)
    {
        value = std::max(value, -89.0f - static_cast<float>(dist) * 1.2f + (random_unit() - 0.5f) * 1.4f);
    }

    return std::max(-124.0f, std::min(-82.0f, value));
}

float sample_hw_rssi(int idx)
{
    if (!s_radio.use_hw || !s_radio.lora)
    {
        return NAN;
    }

    const float freq_mhz = bin_to_freq_mhz(idx);
    s_radio.lora->configureLoraRadio(freq_mhz,
                                     s_radio.bw_khz,
                                     s_radio.sf,
                                     s_radio.cr,
                                     s_radio.tx_power,
                                     s_radio.preamble_len,
                                     s_radio.sync_word,
                                     s_radio.crc_len);
    s_radio.lora->startRadioReceive();
    delay(kSampleSettleMs);

    std::array<float, kSampleCount> values{};
    int valid = 0;
    for (int i = 0; i < kSampleCount; ++i)
    {
        const float rssi = s_radio.lora->getRadioRSSI();
        if (std::isfinite(rssi) && rssi < 0.0f && rssi > -180.0f)
        {
            values[valid++] = rssi;
        }
        delay(kSampleGapMs);
    }

    if (valid <= 0)
    {
        return NAN;
    }

    std::sort(values.begin(), values.begin() + valid);
    return values[valid / 2];
}

float sample_bin_rssi(int idx)
{
    const float hw = sample_hw_rssi(idx);
    if (std::isfinite(hw))
    {
        return hw;
    }
    return simulated_rssi_for_bin(idx);
}

float display_value_for_bin(int idx)
{
    const int bins = active_bin_count();
    if (idx < 0 || idx >= bins)
    {
        return -120.0f;
    }
    const float smooth = s_state.smooth[idx];
    if (smooth < -190.0f)
    {
        return s_state.rssi[idx];
    }
    return smooth;
}

int available_bins_for_metrics()
{
    const int bins = active_bin_count();
    if (s_state.completed_cycles > 0)
    {
        return bins;
    }
    if (s_state.scanned_bins > 0)
    {
        return std::min(s_state.scanned_bins, bins);
    }
    return 1;
}

void recompute_noise_and_hot(int available)
{
    const int bins = active_bin_count();
    available = std::max(1, std::min(available, bins));

    std::vector<float> values;
    values.reserve(static_cast<size_t>(available));
    for (int i = 0; i < available; ++i)
    {
        values.push_back(display_value_for_bin(i));
    }

    const size_t p20 = (values.size() - 1) / 5;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(p20), values.end());
    const float floor_est = values[p20];
    if (!s_state.noise_valid)
    {
        s_state.noise_dbm = floor_est;
        s_state.noise_valid = true;
    }
    else
    {
        s_state.noise_dbm = (kNoiseEmaPrev * s_state.noise_dbm) + (kNoiseEmaNew * floor_est);
    }

    const float hot_enter = s_state.noise_dbm + kHotEnterMarginDb;
    const float hot_exit = s_state.noise_dbm + kHotExitMarginDb;
    for (int i = 0; i < kMaxBins; ++i)
    {
        if (i >= available)
        {
            s_state.hot[i] = 0;
            continue;
        }

        const float v = display_value_for_bin(i);
        if (s_state.hot[i])
        {
            s_state.hot[i] = (v > hot_exit) ? 1 : 0;
        }
        else
        {
            s_state.hot[i] = (v > hot_enter) ? 1 : 0;
        }
    }
}

void recompute_best(int available)
{
    const int bins = active_bin_count();
    available = std::max(1, std::min(available, bins));

    const float step_khz = std::max(1.0f, s_band.step_mhz * 1000.0f);
    const int window_bins = std::max(1, static_cast<int>(std::ceil(s_band.bw_khz / step_khz)));
    const int half_span = ((window_bins - 1) / 2) + kBestGuardBins;

    int best_idx = 0;
    float best_score = 1e9f;
    for (int i = 0; i < available; ++i)
    {
        const int lo = std::max(0, i - half_span);
        const int hi = std::min(available - 1, i + half_span);
        float window_worst = -200.0f;
        for (int j = lo; j <= hi; ++j)
        {
            window_worst = std::max(window_worst, display_value_for_bin(j));
        }
        if (window_worst < best_score)
        {
            best_score = window_worst;
            best_idx = i;
        }
    }

    s_state.best_index = clamp_index(best_idx);
}

void process_scan_step()
{
    if (!s_state.scanning)
    {
        return;
    }

    const int bins = active_bin_count();
    const int idx = clamp_index(s_state.scan_index);
    const float sample = sample_bin_rssi(idx);

    s_state.rssi[idx] = sample;
    const float prev = s_state.smooth[idx];
    if (prev < -190.0f)
    {
        s_state.smooth[idx] = sample;
    }
    else
    {
        s_state.smooth[idx] = (kSweepEmaNew * sample) + (kSweepEmaPrev * prev);
    }

    s_state.cursor_index = idx;
    s_state.scan_index++;
    s_state.scanned_bins = std::max(s_state.scanned_bins, s_state.scan_index);
    s_state.progress = static_cast<float>(s_state.scan_index) / static_cast<float>(bins);

    if (s_state.scan_index >= bins)
    {
        s_state.progress = 1.0f;
        s_state.scan_index = 0;
        s_state.scanned_bins = bins;
        s_state.completed_cycles++;
    }

    const int available = available_bins_for_metrics();
    recompute_noise_and_hot(available);
    recompute_best(available);
    s_state.sim_phase += 0.17f;
}

void set_scan_button_style()
{
    if (!s_ui.btn_scan || !s_ui.btn_scan_label)
    {
        return;
    }

    const uint32_t bg = s_state.scanning ? kColorWarn : kColorAmber;
    const uint32_t border = s_state.scanning ? 0x8A2E1C : kColorAmberDark;
    lv_obj_set_style_bg_color(s_ui.btn_scan, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_ui.btn_scan, lv_color_hex(border), 0);
    lv_obj_set_style_text_color(s_ui.btn_scan_label, lv_color_white(), 0);
    lv_label_set_text(s_ui.btn_scan_label, s_state.scanning ? "STOP" : "SCAN");
    lv_obj_center(s_ui.btn_scan_label);
}

void set_auto_button_style()
{
    if (!s_ui.btn_auto || !s_ui.btn_auto_label)
    {
        return;
    }

    if (s_state.auto_applied)
    {
        lv_obj_set_style_bg_color(s_ui.btn_auto, lv_color_hex(kColorInfo), 0);
        lv_obj_set_style_bg_opa(s_ui.btn_auto, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_ui.btn_auto, lv_color_hex(0x1F4E84), 0);
        lv_obj_set_style_text_color(s_ui.btn_auto_label, lv_color_white(), 0);
    }
    else
    {
        lv_obj_set_style_bg_color(s_ui.btn_auto, lv_color_hex(kColorPanelBg), 0);
        lv_obj_set_style_bg_opa(s_ui.btn_auto, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_ui.btn_auto, lv_color_hex(kColorInfo), 0);
        lv_obj_set_style_text_color(s_ui.btn_auto_label, lv_color_hex(kColorInfo), 0);
    }
}

void refresh_top_status()
{
    if (!s_ui.mode_chip || !s_ui.cad_chip || !s_ui.cad_chip_label)
    {
        return;
    }

    lv_obj_set_style_bg_color(s_ui.mode_chip,
                              lv_color_hex(s_state.scanning ? kColorAmber : 0xD4BE8E),
                              0);
    lv_obj_set_style_border_color(s_ui.mode_chip, lv_color_hex(kColorAmberDark), 0);

    if (s_radio.use_hw)
    {
        const bool blink = s_state.scanning && ((millis() / 450u) % 2u == 0u);
        lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(blink ? kColorInfo : 0x245B95), 0);
        lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(0x1C4B7F), 0);
        lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_white(), 0);
        lv_label_set_text(s_ui.cad_chip_label, "CAD");
    }
    else
    {
        lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(0xD3C8AE), 0);
        lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_hex(kColorTextDim), 0);
        lv_label_set_text(s_ui.cad_chip_label, "SIM");
    }
}

void refresh_plot()
{
    if (!s_ui.plot_area)
    {
        return;
    }

    const int bins = active_bin_count();
    for (int i = 0; i < kMaxBins; ++i)
    {
        lv_obj_t* bar = s_ui.bars[i];
        if (!bar)
        {
            continue;
        }

        if (i >= bins)
        {
            lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);

        const float v = display_value_for_bin(i);
        float t = (v - kRssiFloor) / (kRssiCeil - kRssiFloor);
        if (t < 0.0f)
        {
            t = 0.0f;
        }
        else if (t > 1.0f)
        {
            t = 1.0f;
        }

        const int x0 = (i * kPlotW) / bins;
        const int x1 = ((i + 1) * kPlotW) / bins;
        int w = x1 - x0 - 1;
        if (w < 2)
        {
            w = 2;
        }
        if (x0 + w > kPlotW)
        {
            w = kPlotW - x0;
        }
        if (w <= 0)
        {
            w = 1;
        }

        int h = static_cast<int>(std::round(t * static_cast<float>(kPlotH)));
        if (h < 2)
        {
            h = 2;
        }
        if (h > kPlotH)
        {
            h = kPlotH;
        }

        lv_obj_set_pos(bar, x0, kPlotH - h);
        lv_obj_set_size(bar, w, h);
        lv_obj_set_style_bg_color(bar,
                                  lv_color_hex(s_state.hot[i] ? kColorWarn : kColorAmber),
                                  0);
    }

    const int idx = clamp_index(s_state.cursor_index);
    const int c0 = (idx * kPlotW) / bins;
    const int c1 = ((idx + 1) * kPlotW) / bins;
    const int cx = (c0 + c1) / 2;

    if (s_ui.cursor_line)
    {
        lv_obj_set_pos(s_ui.cursor_line, cx - 1, 0);
        lv_obj_set_size(s_ui.cursor_line, 2, kPlotH);
        lv_obj_move_foreground(s_ui.cursor_line);
    }
    if (s_ui.cursor_tip)
    {
        lv_obj_set_pos(s_ui.cursor_tip, cx - 6, kPlotH - 14);
        lv_obj_move_foreground(s_ui.cursor_tip);
    }
}

void refresh_right_panel_text()
{
    const int cursor = clamp_index(s_state.cursor_index);
    const int best = clamp_index(s_state.best_index);
    const float cursor_freq = bin_to_freq_mhz(cursor);
    const float cursor_rssi = display_value_for_bin(cursor);
    const float best_freq = bin_to_freq_mhz(best);
    const float best_rssi = display_value_for_bin(best);
    const int cleanliness = static_cast<int>(std::lround(std::max(0.0f, s_state.noise_dbm - best_rssi)));

    if (s_ui.cursor_freq)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f", cursor_freq);
        lv_label_set_text(s_ui.cursor_freq, buf);
    }

    if (s_ui.rssi_label)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "RSSI %.0f dBm", cursor_rssi);
        lv_label_set_text(s_ui.rssi_label, buf);
        lv_obj_set_style_text_color(
            s_ui.rssi_label,
            lv_color_hex(s_state.hot[cursor] ? kColorWarn : kColorText),
            0);
    }

    if (s_ui.noise_label)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "NOISE %.0f dBm", s_state.noise_dbm);
        lv_label_set_text(s_ui.noise_label, buf);
    }

    if (s_ui.best_freq)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f", best_freq);
        lv_label_set_text(s_ui.best_freq, buf);
    }

    if (s_ui.best_snr)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "SNR +%d", cleanliness);
        lv_label_set_text(s_ui.best_snr, buf);
    }

    if (s_ui.progress_bar)
    {
        int pct = static_cast<int>(std::lround(s_state.progress * 100.0f));
        if (pct < 0)
        {
            pct = 0;
        }
        if (pct > 100)
        {
            pct = 100;
        }
        lv_bar_set_value(s_ui.progress_bar, pct, LV_ANIM_OFF);

        if (s_ui.progress_pct)
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(s_ui.progress_pct, buf);
        }
    }
}

void refresh_scale_labels()
{
    if (!s_ui.scale_left || !s_ui.scale_mid || !s_ui.scale_right)
    {
        return;
    }

    const int bins = active_bin_count();
    const float end_freq = bin_to_freq_mhz(bins - 1);

    char left[20];
    char right[20];
    char mid[40];

    snprintf(left, sizeof(left), "%.3f", s_band.freq_start_mhz);
    snprintf(right, sizeof(right), "%.3f", end_freq);

    const float step_khz = s_band.step_mhz * 1000.0f;
    const float step_round = std::round(step_khz);
    const float bw_round = std::round(s_band.bw_khz);
    const bool step_int = std::fabs(step_khz - step_round) < 0.05f;
    const bool bw_int = std::fabs(s_band.bw_khz - bw_round) < 0.05f;

    if (step_int && bw_int)
    {
        snprintf(mid, sizeof(mid), "STEP %.0fk | BW %.0fk",
                 static_cast<double>(step_khz),
                 static_cast<double>(s_band.bw_khz));
    }
    else
    {
        snprintf(mid, sizeof(mid), "STEP %.1fk | BW %.1fk",
                 static_cast<double>(step_khz),
                 static_cast<double>(s_band.bw_khz));
    }

    lv_label_set_text(s_ui.scale_left, left);
    lv_label_set_text(s_ui.scale_mid, mid);
    lv_label_set_text(s_ui.scale_right, right);
}

void refresh_all_ui()
{
    refresh_top_status();
    refresh_scale_labels();
    refresh_plot();
    refresh_right_panel_text();
    set_scan_button_style();
    set_auto_button_style();
}

void on_back_requested(lv_event_t*)
{
    ui_request_exit_to_menu();
}

void apply_auto_choice()
{
    s_state.auto_applied = true;
    s_state.cursor_index = clamp_index(s_state.best_index);

    if (s_radio.use_hw && s_radio.lora)
    {
        const float best_freq = bin_to_freq_mhz(s_state.best_index);
        s_radio.lora->configureLoraRadio(best_freq,
                                         s_radio.bw_khz,
                                         s_radio.sf,
                                         s_radio.cr,
                                         s_radio.tx_power,
                                         s_radio.preamble_len,
                                         s_radio.sync_word,
                                         s_radio.crc_len);
        s_radio.lora->startRadioReceive();
    }
}

void on_scan_btn_clicked(lv_event_t*)
{
    s_state.auto_applied = false;
    if (s_state.scanning)
    {
        s_state.scanning = false;
    }
    else
    {
        s_state.scanning = true;
        s_state.scan_index = 0;
        s_state.scanned_bins = 0;
        s_state.completed_cycles = 0;
        s_state.progress = 0.0f;
    }
    refresh_all_ui();
}

void on_auto_btn_clicked(lv_event_t*)
{
    apply_auto_choice();
    refresh_all_ui();
}

void move_cursor_manual(int delta)
{
    if (s_state.scanning)
    {
        return;
    }
    s_state.cursor_index = clamp_index(s_state.cursor_index + delta);
    refresh_all_ui();
}

void handle_key_common(uint32_t key)
{
    if (key == LV_KEY_BACKSPACE)
    {
        on_back_requested(nullptr);
        return;
    }
    if (key == LV_KEY_LEFT)
    {
        move_cursor_manual(-1);
        return;
    }
    if (key == LV_KEY_RIGHT)
    {
        move_cursor_manual(1);
        return;
    }
}

void root_key_event_cb(lv_event_t* e)
{
    handle_key_common(lv_event_get_key(e));
}

void back_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        on_back_requested(nullptr);
        return;
    }
    handle_key_common(key);
}

void scan_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        on_scan_btn_clicked(nullptr);
        return;
    }
    handle_key_common(key);
}

void auto_btn_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ENTER)
    {
        on_auto_btn_clicked(nullptr);
        return;
    }
    handle_key_common(key);
}

void setup_radio_context()
{
    s_radio = {};
    setup_sweep_band_plan();

    app::AppContext& app_ctx = app::AppContext::getInstance();
    const app::AppConfig& cfg = app_ctx.getConfig();
    s_radio.bw_khz = s_band.bw_khz;

    if (cfg.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        const chat::MeshConfig& mesh = cfg.meshcore_config;
        s_radio.sf = (mesh.meshcore_sf >= 5 && mesh.meshcore_sf <= 12) ? mesh.meshcore_sf : 9;
        s_radio.cr = (mesh.meshcore_cr >= 5 && mesh.meshcore_cr <= 8) ? mesh.meshcore_cr : 5;
        s_radio.tx_power = mesh.tx_power;
    }
    else
    {
        const chat::MeshConfig& mesh = cfg.meshtastic_config;
        s_radio.sf = (mesh.spread_factor >= 5 && mesh.spread_factor <= 12) ? mesh.spread_factor : 11;
        s_radio.cr = (mesh.coding_rate >= 5 && mesh.coding_rate <= 8) ? mesh.coding_rate : 5;
        s_radio.tx_power = mesh.tx_power;
    }

    s_radio.lora = app_ctx.getLoraBoard();
    if (!s_radio.lora || !s_radio.lora->isRadioOnline())
    {
        return;
    }

    if (!app::AppTasks::areRadioTasksPaused())
    {
        app::AppTasks::pauseRadioTasks();
        s_radio.paused_by_us = true;
    }
    s_radio.use_hw = true;

    s_radio.lora->configureLoraRadio(s_band.freq_start_mhz,
                                     s_radio.bw_khz,
                                     s_radio.sf,
                                     s_radio.cr,
                                     s_radio.tx_power,
                                     s_radio.preamble_len,
                                     s_radio.sync_word,
                                     s_radio.crc_len);
    s_radio.lora->startRadioReceive();
}

void teardown_radio_context()
{
    if (!s_radio.use_hw)
    {
        s_radio = {};
        return;
    }

    if (s_radio.paused_by_us)
    {
        app::AppContext::getInstance().applyMeshConfig();
        app::AppTasks::resumeRadioTasks();
    }

    s_radio = {};
}

void init_sweep_state()
{
    s_state = {};
    const int bins = active_bin_count();
    s_state.scanning = true;
    s_state.noise_dbm = -104.0f;
    s_state.noise_valid = true;
    s_state.rand_state ^= static_cast<uint32_t>(millis());
    s_state.sim_phase = random_unit() * 37.0f;
    s_state.cursor_index = bins / 2;

    for (int i = 0; i < kMaxBins; ++i)
    {
        if (i < bins)
        {
            const float v = simulated_rssi_for_bin(i);
            s_state.rssi[i] = v;
            s_state.smooth[i] = v;
        }
        else
        {
            s_state.rssi[i] = -200.0f;
            s_state.smooth[i] = -200.0f;
        }
        s_state.hot[i] = 0;
    }
    s_state.scanned_bins = bins;
    s_state.completed_cycles = 1;
    s_state.scan_index = 0;
    s_state.progress = 0.0f;
    recompute_noise_and_hot(bins);
    recompute_best(bins);
}

void refresh_timer_cb(lv_timer_t*)
{
    if (!s_ui.root)
    {
        return;
    }

    process_scan_step();
    refresh_all_ui();
}

void build_topbar(lv_obj_t* root)
{
    s_ui.topbar = lv_obj_create(root);
    lv_obj_set_pos(s_ui.topbar, 0, 0);
    lv_obj_set_size(s_ui.topbar, kScreenW, kTopBarH);
    lv_obj_set_style_bg_color(s_ui.topbar, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.topbar, 0, 0);
    lv_obj_set_style_pad_all(s_ui.topbar, 0, 0);
    lv_obj_clear_flag(s_ui.topbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bottom_line = lv_obj_create(s_ui.topbar);
    lv_obj_set_pos(bottom_line, 0, kTopBarH - 2);
    lv_obj_set_size(bottom_line, kScreenW, 2);
    lv_obj_set_style_bg_color(bottom_line, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_bg_opa(bottom_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_line, 0, 0);
    lv_obj_set_style_radius(bottom_line, 0, 0);
    lv_obj_clear_flag(bottom_line, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.back_btn = lv_btn_create(s_ui.topbar);
    lv_obj_set_pos(s_ui.back_btn, 8, 4);
    lv_obj_set_size(s_ui.back_btn, 28, 20);
    lv_obj_set_style_bg_color(s_ui.back_btn, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.back_btn, 1, 0);
    lv_obj_set_style_border_color(s_ui.back_btn, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.back_btn, 10, 0);
    lv_obj_set_style_outline_width(s_ui.back_btn, 0, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_ui.back_btn, on_back_requested, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ui.back_btn, back_btn_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* back_label = lv_label_create(s_ui.back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(kColorText), 0);
    lv_obj_center(back_label);

    s_ui.title = lv_label_create(s_ui.topbar);
    lv_label_set_text(s_ui.title, "SUB-GHz SCAN");
    lv_obj_set_style_text_font(s_ui.title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.title, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.title, 46, 0);

    s_ui.mode_chip = lv_obj_create(s_ui.topbar);
    lv_obj_set_pos(s_ui.mode_chip, 264, 5);
    lv_obj_set_size(s_ui.mode_chip, 118, 18);
    lv_obj_set_style_bg_color(s_ui.mode_chip, lv_color_hex(kColorAmber), 0);
    lv_obj_set_style_bg_opa(s_ui.mode_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.mode_chip, 1, 0);
    lv_obj_set_style_border_color(s_ui.mode_chip, lv_color_hex(kColorAmberDark), 0);
    lv_obj_set_style_radius(s_ui.mode_chip, 4, 0);
    lv_obj_set_style_pad_all(s_ui.mode_chip, 0, 0);
    lv_obj_clear_flag(s_ui.mode_chip, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.mode_chip_label = lv_label_create(s_ui.mode_chip);
    lv_label_set_text(s_ui.mode_chip_label, "MODE: RSSI");
    lv_obj_set_style_text_font(s_ui.mode_chip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.mode_chip_label, lv_color_hex(kColorText), 0);
    lv_obj_center(s_ui.mode_chip_label);

    s_ui.cad_chip = lv_obj_create(s_ui.topbar);
    lv_obj_set_pos(s_ui.cad_chip, 388, 5);
    lv_obj_set_size(s_ui.cad_chip, 82, 18);
    lv_obj_set_style_bg_color(s_ui.cad_chip, lv_color_hex(kColorInfo), 0);
    lv_obj_set_style_bg_opa(s_ui.cad_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.cad_chip, 1, 0);
    lv_obj_set_style_border_color(s_ui.cad_chip, lv_color_hex(0x1C4B7F), 0);
    lv_obj_set_style_radius(s_ui.cad_chip, 4, 0);
    lv_obj_set_style_pad_all(s_ui.cad_chip, 0, 0);
    lv_obj_clear_flag(s_ui.cad_chip, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.cad_chip_label = lv_label_create(s_ui.cad_chip);
    lv_label_set_text(s_ui.cad_chip_label, "CAD");
    lv_obj_set_style_text_font(s_ui.cad_chip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.cad_chip_label, lv_color_white(), 0);
    lv_obj_center(s_ui.cad_chip_label);
}

void build_left_panel(lv_obj_t* root)
{
    s_ui.left_panel = lv_obj_create(root);
    lv_obj_set_pos(s_ui.left_panel, kLeftPanelX, kLeftPanelY);
    lv_obj_set_size(s_ui.left_panel, kLeftPanelW, kLeftPanelH);
    lv_obj_set_style_bg_color(s_ui.left_panel, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.left_panel, 2, 0);
    lv_obj_set_style_border_color(s_ui.left_panel, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.left_panel, 0, 0);
    lv_obj_set_style_pad_all(s_ui.left_panel, 0, 0);
    lv_obj_clear_flag(s_ui.left_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.plot_area = lv_obj_create(s_ui.left_panel);
    lv_obj_set_pos(s_ui.plot_area, kPlotX, kPlotY);
    lv_obj_set_size(s_ui.plot_area, kPlotW, kPlotH);
    lv_obj_set_style_bg_color(s_ui.plot_area, lv_color_hex(0xF2E4C8), 0);
    lv_obj_set_style_bg_opa(s_ui.plot_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.plot_area, 1, 0);
    lv_obj_set_style_border_color(s_ui.plot_area, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.plot_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.plot_area, 0, 0);
    lv_obj_clear_flag(s_ui.plot_area, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 1; i <= 4; ++i)
    {
        lv_obj_t* grid = lv_obj_create(s_ui.plot_area);
        lv_obj_set_pos(grid, 0, (i * kPlotH) / 5);
        lv_obj_set_size(grid, kPlotW, 1);
        lv_obj_set_style_bg_color(grid, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_50, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_radius(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int i = 0; i < kMaxBins; ++i)
    {
        lv_obj_t* bar = lv_obj_create(s_ui.plot_area);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kColorAmber), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        s_ui.bars[i] = bar;
    }

    s_ui.cursor_line = lv_obj_create(s_ui.plot_area);
    lv_obj_set_size(s_ui.cursor_line, 2, kPlotH);
    lv_obj_set_style_bg_color(s_ui.cursor_line, lv_color_hex(kColorInfo), 0);
    lv_obj_set_style_bg_opa(s_ui.cursor_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.cursor_line, 0, 0);
    lv_obj_set_style_radius(s_ui.cursor_line, 0, 0);
    lv_obj_clear_flag(s_ui.cursor_line, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.cursor_tip = lv_label_create(s_ui.plot_area);
    lv_label_set_text(s_ui.cursor_tip, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(s_ui.cursor_tip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.cursor_tip, lv_color_hex(kColorInfo), 0);

    lv_obj_t* scale_bar = lv_obj_create(s_ui.left_panel);
    lv_obj_set_pos(scale_bar, kScaleBarX, kScaleBarY);
    lv_obj_set_size(scale_bar, kScaleBarW, kScaleBarH);
    lv_obj_set_style_bg_opa(scale_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scale_bar, 1, 0);
    lv_obj_set_style_border_color(scale_bar, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_border_side(scale_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(scale_bar, 0, 0);
    lv_obj_set_style_radius(scale_bar, 0, 0);
    lv_obj_clear_flag(scale_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.scale_left = lv_label_create(scale_bar);
    lv_label_set_text(s_ui.scale_left, "----");
    lv_obj_set_style_text_font(s_ui.scale_left, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.scale_left, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.scale_left, 2, 6);

    s_ui.scale_mid = lv_label_create(scale_bar);
    lv_label_set_text(s_ui.scale_mid, "STEP -- | BW --");
    lv_obj_set_style_text_font(s_ui.scale_mid, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.scale_mid, lv_color_hex(kColorTextDim), 0);
    lv_obj_align(s_ui.scale_mid, LV_ALIGN_CENTER, 0, 5);

    s_ui.scale_right = lv_label_create(scale_bar);
    lv_label_set_text(s_ui.scale_right, "----");
    lv_obj_set_style_text_font(s_ui.scale_right, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.scale_right, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_align(s_ui.scale_right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_ui.scale_right, 84);
    lv_obj_set_pos(s_ui.scale_right, kScaleBarW - 86, 6);
}

void build_right_panel(lv_obj_t* root)
{
    s_ui.right_panel = lv_obj_create(root);
    lv_obj_set_pos(s_ui.right_panel, kRightPanelX, kRightPanelY);
    lv_obj_set_size(s_ui.right_panel, kRightPanelW, kRightPanelH);
    lv_obj_set_style_bg_color(s_ui.right_panel, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.right_panel, 2, 0);
    lv_obj_set_style_border_color(s_ui.right_panel, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.right_panel, 0, 0);
    lv_obj_set_style_pad_all(s_ui.right_panel, 0, 0);
    lv_obj_clear_flag(s_ui.right_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* sep1 = lv_obj_create(s_ui.right_panel);
    lv_obj_set_pos(sep1, 0, 76);
    lv_obj_set_size(sep1, kRightPanelW, 1);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_80, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_cursor = lv_label_create(s_ui.right_panel);
    lv_label_set_text(title_cursor, "CURSOR");
    lv_obj_set_style_text_font(title_cursor, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_cursor, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(title_cursor, 8, 2);

    s_ui.cursor_freq = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.cursor_freq, "433.550");
    lv_obj_set_style_text_font(s_ui.cursor_freq, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.cursor_freq, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.cursor_freq, 8, 14);

    s_ui.cursor_unit = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.cursor_unit, "MHz");
    lv_obj_set_style_text_font(s_ui.cursor_unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.cursor_unit, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.cursor_unit, 84, 22);

    s_ui.rssi_label = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.rssi_label, "RSSI -92 dBm");
    lv_obj_set_style_text_font(s_ui.rssi_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.rssi_label, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(s_ui.rssi_label, 8, 43);

    s_ui.noise_label = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.noise_label, "NOISE -104 dBm");
    lv_obj_set_style_text_font(s_ui.noise_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.noise_label, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.noise_label, 8, 60);

    lv_obj_t* title_best = lv_label_create(s_ui.right_panel);
    lv_label_set_text(title_best, "BEST");
    lv_obj_set_style_text_font(title_best, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_best, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(title_best, 8, 80);

    s_ui.best_freq = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.best_freq, "434.125");
    lv_obj_set_style_text_font(s_ui.best_freq, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.best_freq, lv_color_hex(kColorOk), 0);
    lv_obj_set_pos(s_ui.best_freq, 8, 97);

    s_ui.best_snr = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.best_snr, "SNR +12");
    lv_obj_set_style_text_font(s_ui.best_snr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.best_snr, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.best_snr, 8, 114);

    s_ui.progress_bar = lv_bar_create(s_ui.right_panel);
    lv_obj_set_pos(s_ui.progress_bar, 8, 120);
    lv_obj_set_size(s_ui.progress_bar, 66, 12);
    lv_bar_set_range(s_ui.progress_bar, 0, 100);
    lv_bar_set_value(s_ui.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.progress_bar, lv_color_hex(kColorPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.progress_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.progress_bar, lv_color_hex(kColorLine), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.progress_bar, lv_color_hex(kColorAmberDark), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.progress_bar, 0, LV_PART_INDICATOR);

    s_ui.progress_pct = lv_label_create(s_ui.right_panel);
    lv_label_set_text(s_ui.progress_pct, "0%");
    lv_obj_set_style_text_font(s_ui.progress_pct, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ui.progress_pct, lv_color_hex(kColorTextDim), 0);
    lv_obj_set_pos(s_ui.progress_pct, 78, 118);

    s_ui.btn_scan = lv_btn_create(s_ui.right_panel);
    lv_obj_set_pos(s_ui.btn_scan, 8, 134);
    lv_obj_set_size(s_ui.btn_scan, 46, 28);
    lv_obj_set_style_bg_color(s_ui.btn_scan, lv_color_hex(kColorWarn), 0);
    lv_obj_set_style_bg_opa(s_ui.btn_scan, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.btn_scan, 1, 0);
    lv_obj_set_style_border_color(s_ui.btn_scan, lv_color_hex(0x8A2E1C), 0);
    lv_obj_set_style_radius(s_ui.btn_scan, 5, 0);
    lv_obj_set_style_outline_width(s_ui.btn_scan, 0, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_ui.btn_scan, on_scan_btn_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ui.btn_scan, scan_btn_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.btn_scan_label = lv_label_create(s_ui.btn_scan);
    lv_label_set_text(s_ui.btn_scan_label, "STOP");
    lv_obj_set_style_text_font(s_ui.btn_scan_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.btn_scan_label, lv_color_white(), 0);
    lv_obj_center(s_ui.btn_scan_label);

    s_ui.btn_auto = lv_btn_create(s_ui.right_panel);
    lv_obj_set_pos(s_ui.btn_auto, 60, 134);
    lv_obj_set_size(s_ui.btn_auto, 46, 28);
    lv_obj_set_style_bg_color(s_ui.btn_auto, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.btn_auto, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.btn_auto, 1, 0);
    lv_obj_set_style_border_color(s_ui.btn_auto, lv_color_hex(kColorInfo), 0);
    lv_obj_set_style_radius(s_ui.btn_auto, 5, 0);
    lv_obj_set_style_outline_width(s_ui.btn_auto, 0, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_ui.btn_auto, on_auto_btn_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_ui.btn_auto, auto_btn_key_event_cb, LV_EVENT_KEY, nullptr);

    s_ui.btn_auto_label = lv_label_create(s_ui.btn_auto);
    lv_label_set_text(s_ui.btn_auto_label, "AUTO");
    lv_obj_set_style_text_font(s_ui.btn_auto_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.btn_auto_label, lv_color_hex(kColorInfo), 0);
    lv_obj_center(s_ui.btn_auto_label);
}

void reset_ui_state()
{
    s_ui = {};
}

} // namespace

lv_obj_t* ui_energy_sweep_create(lv_obj_t* parent)
{
    if (!parent)
    {
        return nullptr;
    }
    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    build_topbar(s_ui.root);
    build_left_panel(s_ui.root);
    build_right_panel(s_ui.root);
    refresh_all_ui();

    return s_ui.root;
}

void ui_energy_sweep_enter(lv_obj_t* parent)
{
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    setup_radio_context();
    init_sweep_state();
    ui_energy_sweep_create(parent);

    if (::app_g && s_ui.back_btn)
    {
        lv_group_remove_all_objs(::app_g);
        lv_group_add_obj(::app_g, s_ui.back_btn);
        if (s_ui.btn_scan)
        {
            lv_group_add_obj(::app_g, s_ui.btn_scan);
        }
        if (s_ui.btn_auto)
        {
            lv_group_add_obj(::app_g, s_ui.btn_auto);
        }
        lv_group_focus_obj(s_ui.back_btn);
        set_default_group(::app_g);
        lv_group_set_editing(::app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    disableScreenSleep();

    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_timer_cb, kScanIntervalMs, nullptr);
    }
    refresh_all_ui();
}

void ui_energy_sweep_exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_refresh_timer)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }

    teardown_radio_context();
    enableScreenSleep();

    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_state();
    }
}
