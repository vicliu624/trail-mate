#include "sstv_service.h"

#include <Arduino.h>

#ifndef SSTV_DEBUG
#define SSTV_DEBUG 1
#endif

#if SSTV_DEBUG
#define SSTV_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define SSTV_LOG(...) ((void)0)
#endif

#ifndef SSTV_VERBOSE
#define SSTV_VERBOSE 0
#endif

#if SSTV_VERBOSE
#define SSTV_LOG_V(...) SSTV_LOG(__VA_ARGS__)
#else
#define SSTV_LOG_V(...) ((void)0)
#endif

#ifndef SSTV_VIS_DIAG
#define SSTV_VIS_DIAG 1
#endif

#if SSTV_VIS_DIAG
#define SSTV_LOG_VIS(...) SSTV_LOG(__VA_ARGS__)
#else
#define SSTV_LOG_VIS(...) ((void)0)
#endif

#if defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)

#include "../board/TLoRaPagerBoard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SD.h>
#include <cmath>
#include <cstring>
#include <ctime>

namespace
{
constexpr uint32_t kSampleRate = 44100;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kChannels = 1;
constexpr float kMicGainDb = 24.0f;
constexpr uint32_t kTaskStack = 8192;
constexpr uint32_t kTaskDelayMs = 2;

constexpr float kPorchMs = 1.5f;
constexpr float kSyncPulseMs = 9.0f;
constexpr float kColorMsScottie1 = 138.24f;
constexpr float kColorMsScottie2 = 88.064f;
constexpr float kColorMsScottieDX = 345.6f;
constexpr float kRobotSyncPulseMs = 9.0f;
constexpr float kRobotSyncPorchMs = 3.0f;
constexpr float kRobotSepMs = 4.5f;
constexpr float kRobotPorchMs = 1.5f;
constexpr float kRobotYMs = 138.0f;
constexpr float kRobotChromaMs = 69.0f;
constexpr float kRobot36YMs = 88.0f;
constexpr float kRobot36ChromaMs = 44.0f;
constexpr float kMartinSyncMs = 4.862f;
constexpr float kMartinPorchMs = 0.572f;
constexpr float kColorMsMartin1 = 146.432f;
constexpr float kColorMsMartin2 = 73.216f;
constexpr float kPdSyncMs = 20.0f;
constexpr float kPdPorchMs = 2.080f;
constexpr float kPd50ScanMs = 91.520f;
constexpr float kPd90ScanMs = 170.240f;
constexpr float kPd120ScanMs = 121.600f;
constexpr float kPd160ScanMs = 195.584f;
constexpr float kPd180ScanMs = 183.040f;
constexpr float kPd240ScanMs = 244.480f;
constexpr float kPd290ScanMs = 228.800f;
constexpr float kP3SyncMs = 5.208f;
constexpr float kP5SyncMs = 7.813f;
constexpr float kP7SyncMs = 10.417f;
constexpr float kP3PorchMs = 1.042f;
constexpr float kP5PorchMs = 1.563f;
constexpr float kP7PorchMs = 2.083f;
constexpr float kP3ColorMs = 133.333f;
constexpr float kP5ColorMs = 200.000f;
constexpr float kP7ColorMs = 266.666f;
constexpr float kLeaderMs = 300.0f;
constexpr float kBreakMs = 10.0f;
constexpr float kVisBitMs = 30.0f;
constexpr float kVisPairRatio = 0.08f;
constexpr uint8_t kVisScottie1 = 60;
constexpr uint8_t kVisScottie2 = 56;
constexpr uint8_t kVisScottieDX = 76;
constexpr uint8_t kVisRobot72 = 12;
constexpr uint8_t kVisRobot36 = 8;
constexpr uint8_t kVisMartin1 = 44;
constexpr uint8_t kVisMartin2 = 40;
constexpr uint8_t kVisPd50 = 93;
constexpr uint8_t kVisPd90 = 99;
constexpr uint8_t kVisPd120 = 95;
constexpr uint8_t kVisPd160 = 98;
constexpr uint8_t kVisPd180 = 96;
constexpr uint8_t kVisPd240 = 97;
constexpr uint8_t kVisPd290 = 94;
constexpr uint8_t kVisP3 = 113;
constexpr uint8_t kVisP5 = 114;
constexpr uint8_t kVisP7 = 115;

constexpr int kInWidth = 320;
constexpr int kInHeightScottie = 256;
constexpr int kInHeightRobot72 = 240;
constexpr int kInHeightPd120 = 496;
constexpr int kInHeightPd160 = 400;
constexpr int kInHeightPd290 = 616;
constexpr int kInHeightPasokon = 496;
constexpr int kOutWidth = 288;
constexpr int kOutHeight = 192;
constexpr int kOutImageWidth = 240;
constexpr int kPadX = (kOutWidth - kOutImageWidth) / 2;

constexpr uint32_t kPanelBg = 0xFAF0D8;

constexpr float kFreqMin = 1500.0f;
constexpr float kFreqMax = 2300.0f;
constexpr float kFreqSpan = kFreqMax - kFreqMin;

constexpr float kMinSyncGapMs = 420.0f;
constexpr float kSyncTimeoutMs = 2000.0f;
constexpr float kAudioResetLevel = 0.03f;
constexpr float kAudioResetHoldMs = 1500.0f;
constexpr float kHeaderToneDetectRatio = 1.3f;
constexpr float kHeaderToneTotalRatio = 0.45f;
constexpr float kSyncToneDetectRatio = 1.6f;
constexpr float kSyncToneTotalRatio = 0.55f;
constexpr int kHeaderWindowSamples = 256;
constexpr int kHeaderHopSamples = 128;
constexpr int kSyncWindowSamples = 400;
constexpr int kSyncHopSamples = 80;
constexpr float kPixelBinStep = 25.0f;
constexpr int kPixelBinCount = static_cast<int>((kFreqMax - kFreqMin) / kPixelBinStep) + 1;
constexpr int kMaxPixelSamples = 64;

enum class Phase : uint8_t
{
    Idle = 0,
    Porch1,
    Green,
    Porch2,
    Blue,
    Sync,
    Porch3,
    Red,
    RobotSync,
    RobotPorch1,
    RobotY,
    RobotSep1,
    RobotPorch2,
    RobotRY,
    RobotSep2,
    RobotPorch3,
    RobotBY,
    MartinSync,
    MartinPorch,
    MartinGreen,
    MartinSep1,
    MartinBlue,
    MartinSep2,
    MartinRed,
    MartinSep3,
    PdSync,
    PdPorch,
    PdY1,
    PdRY,
    PdBY,
    PdY2,
    PSync,
    PPorch1,
    PRed,
    PPorch2,
    PGreen,
    PPorch3,
    PBlue,
    PPorch4,
};

enum class HeaderState : uint8_t
{
    SeekLeader1 = 0,
    SeekBreak,
    SeekLeader2,
    SeekVisStart,
    ReadVisBits,
};

enum class Tone : uint8_t
{
    None = 0,
    Tone1100,
    Tone1200,
    Tone1300,
    Tone1900,
};

enum class VisMode : uint8_t
{
    Unknown = 0,
    Scottie1,
    Scottie2,
    ScottieDX,
    Robot72,
    Robot36,
    Martin1,
    Martin2,
    Pd50,
    Pd90,
    Pd120,
    Pd160,
    Pd180,
    Pd240,
    Pd290,
    P3,
    P5,
    P7,
};

struct GoertzelBin
{
    float freq = 0.0f;
    float coeff = 0.0f;
    float cos_w = 0.0f;
    float sin_w = 0.0f;
};

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_task = nullptr;
volatile bool s_stop = false;
bool s_active = false;
bool s_codec_open = false;

sstv::Status s_status;
char s_last_error[96] = {0};
char s_saved_path[64] = {0};
bool s_pending_save = false;
VisMode s_vis_mode = VisMode::Unknown;
int s_line_count = kInHeightScottie;

uint16_t* s_frame = nullptr;
int s_last_output_y = -1;

uint32_t s_accum[3][kInWidth];
uint16_t s_count[3][kInWidth];
uint8_t s_last_ry[kInWidth];
uint8_t s_last_by[kInWidth];
bool s_has_ry = false;
bool s_has_by = false;
uint8_t s_pd_y1[kInWidth];
bool s_pd_has_y1 = false;
bool s_pd_use_y1 = false;

GoertzelBin s_bin_1100 = {};
GoertzelBin s_bin_1200 = {};
GoertzelBin s_bin_1300 = {};
GoertzelBin s_bin_1900 = {};
GoertzelBin s_pixel_bins[kPixelBinCount] = {};

uint16_t rgb_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = static_cast<uint16_t>(r >> 3);
    uint16_t g6 = static_cast<uint16_t>(g >> 2);
    uint16_t b5 = static_cast<uint16_t>(b >> 3);
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

uint16_t panel_rgb565()
{
    uint8_t r = static_cast<uint8_t>((kPanelBg >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((kPanelBg >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(kPanelBg & 0xFF);
    return rgb_to_565(r, g, b);
}

GoertzelBin make_bin(float freq)
{
    GoertzelBin bin;
    bin.freq = freq;
    float w = 2.0f * static_cast<float>(M_PI) * freq / static_cast<float>(kSampleRate);
    bin.cos_w = cosf(w);
    bin.sin_w = sinf(w);
    bin.coeff = 2.0f * bin.cos_w;
    return bin;
}

float goertzel_power(const int16_t* data, int count, const GoertzelBin& bin)
{
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        q0 = bin.coeff * q1 - q2 + static_cast<float>(data[i]);
        q2 = q1;
        q1 = q0;
    }
    float real = q1 - q2 * bin.cos_w;
    float imag = q2 * bin.sin_w;
    return real * real + imag * imag;
}

float estimate_freq_from_bins(const int16_t* data, int count, const GoertzelBin* bins, int bin_count)
{
    if (!bins || bin_count <= 0)
    {
        return kFreqMin;
    }

    float max_val = 0.0f;
    int max_idx = 0;
    float mags[kPixelBinCount];
    for (int i = 0; i < bin_count; ++i)
    {
        mags[i] = goertzel_power(data, count, bins[i]);
        if (mags[i] > max_val)
        {
            max_val = mags[i];
            max_idx = i;
        }
    }

    int left = max_idx > 0 ? max_idx - 1 : max_idx;
    int right = max_idx + 1 < bin_count ? max_idx + 1 : max_idx;
    float y1 = mags[left];
    float y2 = mags[max_idx];
    float y3 = mags[right];
    float denom = y1 + y2 + y3;
    float peak = static_cast<float>(max_idx);
    if (denom > 0.0f)
    {
        peak = peak + (y3 - y1) / denom;
    }

    float freq = kFreqMin + peak * kPixelBinStep;
    if (freq < kFreqMin)
    {
        freq = kFreqMin;
    }
    if (freq > kFreqMax)
    {
        freq = kFreqMax;
    }
    return freq;
}

Tone detect_tone(float p1100, float p1200, float p1300, float p1900)
{
    float total = p1100 + p1200 + p1300 + p1900;
    if (total <= 0.0f)
    {
        return Tone::None;
    }

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

const char* vis_mode_name(VisMode mode)
{
    switch (mode)
    {
    case VisMode::Scottie1:
        return "Scottie 1";
    case VisMode::Scottie2:
        return "Scottie 2";
    case VisMode::ScottieDX:
        return "Scottie DX";
    case VisMode::Robot72:
        return "Robot 72";
    case VisMode::Robot36:
        return "Robot 36";
    case VisMode::Martin1:
        return "Martin 1";
    case VisMode::Martin2:
        return "Martin 2";
    case VisMode::Pd50:
        return "PD50";
    case VisMode::Pd90:
        return "PD90";
    case VisMode::Pd120:
        return "PD120";
    case VisMode::Pd160:
        return "PD160";
    case VisMode::Pd180:
        return "PD180";
    case VisMode::Pd240:
        return "PD240";
    case VisMode::Pd290:
        return "PD290";
    case VisMode::P3:
        return "P3";
    case VisMode::P5:
        return "P5";
    case VisMode::P7:
        return "P7";
    default:
        return "Unknown";
    }
}

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

int calc_pixel_window_samples(int color_samples_in)
{
    int samples = color_samples_in / kInWidth;
    if (samples < 8)
    {
        samples = 8;
    }
    if (samples > kMaxPixelSamples)
    {
        samples = kMaxPixelSamples;
    }
    return samples;
}

void update_color_timing(float color_ms, int& color_samples, int& pixel_window_samples)
{
    color_samples = static_cast<int>(kSampleRate * (color_ms / 1000.0f));
    pixel_window_samples = calc_pixel_window_samples(color_samples);
}

void update_pd_timing(float scan_ms, int& pd_scan_samples, int& pd_pixel_window_samples)
{
    pd_scan_samples = static_cast<int>(kSampleRate * (scan_ms / 1000.0f));
    pd_pixel_window_samples = calc_pixel_window_samples(pd_scan_samples);
}

void update_p_timing(float sync_ms, float porch_ms, float color_ms_in,
                     int& p_sync_samples, int& p_porch_samples, int& p_color_samples,
                     int& p_pixel_window_samples)
{
    p_sync_samples = static_cast<int>(kSampleRate * (sync_ms / 1000.0f));
    p_porch_samples = static_cast<int>(kSampleRate * (porch_ms / 1000.0f));
    p_color_samples = static_cast<int>(kSampleRate * (color_ms_in / 1000.0f));
    p_pixel_window_samples = calc_pixel_window_samples(p_color_samples);
}

struct ModeContext
{
    VisMode& vis_mode;
    Phase& phase;
    int& phase_samples;
    int& line_index;
    int& line_count;
    int& last_pixel;
    int& pixel_pos;
    int& pixel_fill;
    int32_t& mono;
    float& audio_level;
    bool& header_ok;
    int16_t* pixel_buf;
    int16_t* pixel_window;
    const int& porch_samples;
    const int& sync_samples;
    int& color_samples;
    int& pixel_window_samples;
    const int& robot_sync_samples;
    const int& robot_sync_porch_samples;
    const int& robot_sep_samples;
    const int& robot_porch_samples;
    int& robot_y_samples;
    int& robot_chroma_samples;
    int& robot36_y_samples;
    int& robot36_chroma_samples;
    int& robot_pixel_window_y;
    int& robot_pixel_window_c;
    int& robot36_pixel_window_y;
    int& robot36_pixel_window_c;
    const int& martin_sync_samples;
    const int& martin_porch_samples;
    const int& pd_sync_samples;
    const int& pd_porch_samples;
    int& pd_scan_samples;
    int& pd_pixel_window_samples;
    int& p_sync_samples;
    int& p_porch_samples;
    int& p_color_samples;
    int& p_pixel_window_samples;
    HeaderState& header_state;
    int& header_count;
    int& vis_bit_index;
    uint8_t& vis_value;
    int& vis_ones;
    int& vis_window_count;
    int& vis_1100_count;
    int& vis_1300_count;
    int& vis_valid_windows;
    float& vis_1100_sum;
    float& vis_1300_sum;
    int& vis_skip_windows;
    int& vis_start_count;
    int& vis_start_samples;
    int& vis_bit_samples;
    float& color_ms;
};

void clear_robot_chroma();
void clear_pd_state();

void reset_pixel_state(ModeContext& ctx)
{
    ctx.last_pixel = -1;
    ctx.pixel_pos = 0;
    ctx.pixel_fill = 0;
}

void reset_header_state(ModeContext& ctx)
{
    ctx.header_state = HeaderState::SeekLeader1;
    ctx.header_count = 0;
    ctx.vis_bit_index = 0;
    ctx.vis_value = 0;
    ctx.vis_ones = 0;
    ctx.vis_window_count = 0;
    ctx.vis_1100_count = 0;
    ctx.vis_1300_count = 0;
    ctx.vis_valid_windows = 0;
    ctx.vis_1100_sum = 0.0f;
    ctx.vis_1300_sum = 0.0f;
    ctx.vis_skip_windows = 0;
    ctx.vis_start_count = 0;
    ctx.vis_start_samples = 0;
    ctx.vis_bit_samples = 0;
    ctx.vis_mode = VisMode::Unknown;
    s_vis_mode = VisMode::Unknown;
    ctx.line_count = kInHeightScottie;
    s_line_count = ctx.line_count;
    ctx.color_ms = kColorMsScottie1;
    update_color_timing(ctx.color_ms, ctx.color_samples, ctx.pixel_window_samples);
    clear_robot_chroma();
    clear_pd_state();
}

struct ModeStrategy
{
    const char* name;
    void (*step)(ModeContext&);
};

void step_robot_mode(ModeContext& ctx);
void step_martin_mode(ModeContext& ctx);
void step_pd_mode(ModeContext& ctx);
void step_p_mode(ModeContext& ctx);
void step_scottie_mode(ModeContext& ctx);

const ModeStrategy& select_mode_strategy(VisMode mode)
{
    static const ModeStrategy kRobot = {"robot", step_robot_mode};
    static const ModeStrategy kMartin = {"martin", step_martin_mode};
    static const ModeStrategy kPd = {"pd", step_pd_mode};
    static const ModeStrategy kP = {"p", step_p_mode};
    static const ModeStrategy kScottie = {"scottie", step_scottie_mode};

    switch (mode)
    {
    case VisMode::Robot72:
    case VisMode::Robot36:
        return kRobot;
    case VisMode::Martin1:
    case VisMode::Martin2:
        return kMartin;
    case VisMode::Pd50:
    case VisMode::Pd90:
    case VisMode::Pd120:
    case VisMode::Pd160:
    case VisMode::Pd180:
    case VisMode::Pd240:
    case VisMode::Pd290:
        return kPd;
    case VisMode::P3:
    case VisMode::P5:
    case VisMode::P7:
        return kP;
    default:
        return kScottie;
    }
}

void set_error(const char* msg)
{
    if (!msg)
    {
        s_last_error[0] = '\0';
        return;
    }
    snprintf(s_last_error, sizeof(s_last_error), "%s", msg);
}

void clear_saved_path()
{
    s_saved_path[0] = '\0';
}

const char* get_saved_path()
{
    return s_saved_path;
}

bool ensure_sstv_dir()
{
    if (!SD.exists("/sstv"))
    {
        if (!SD.mkdir("/sstv"))
        {
            return false;
        }
    }
    return true;
}

bool build_save_path(char* out_path, size_t out_len)
{
    if (!out_path || out_len == 0)
    {
        return false;
    }
    time_t now = time(nullptr);
    struct tm* info = nullptr;
    if (now > 0)
    {
        info = gmtime(&now);
    }

    char date_buf[16] = {0};
    if (info)
    {
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", info);
    }

    for (int i = 1; i <= 999; ++i)
    {
        if (info)
        {
            snprintf(out_path, out_len, "/sstv/%s_%03d.bmp", date_buf, i);
        }
        else
        {
            snprintf(out_path, out_len, "/sstv/%lu_%03d.bmp",
                     static_cast<unsigned long>(millis()), i);
        }
        if (!SD.exists(out_path))
        {
            return true;
        }
    }

    return false;
}

bool save_frame_to_sd()
{
    if (!s_frame)
    {
        set_error("No frame");
        return false;
    }
    if (SD.cardType() == CARD_NONE)
    {
        set_error("SD not ready");
        return false;
    }
    if (!ensure_sstv_dir())
    {
        set_error("SD mkdir failed");
        return false;
    }

    char path[64];
    if (!build_save_path(path, sizeof(path)))
    {
        set_error("SD path failed");
        return false;
    }

    const uint32_t w = kOutWidth;
    const uint32_t h = kOutHeight;
    const uint32_t row24 = (w * 3 + 3) & ~3u;
    const uint32_t pixel_bytes = row24 * h;
    const uint32_t file_size = 14 + 40 + pixel_bytes;
    const uint32_t data_offset = 14 + 40;

    File f = SD.open(path, FILE_WRITE);
    if (!f)
    {
        set_error("SD open failed");
        return false;
    }

    uint8_t file_hdr[14] = {
        'B', 'M',
        static_cast<uint8_t>(file_size & 0xFF),
        static_cast<uint8_t>((file_size >> 8) & 0xFF),
        static_cast<uint8_t>((file_size >> 16) & 0xFF),
        static_cast<uint8_t>((file_size >> 24) & 0xFF),
        0, 0, 0, 0,
        static_cast<uint8_t>(data_offset & 0xFF),
        static_cast<uint8_t>((data_offset >> 8) & 0xFF),
        static_cast<uint8_t>((data_offset >> 16) & 0xFF),
        static_cast<uint8_t>((data_offset >> 24) & 0xFF)};
    f.write(file_hdr, sizeof(file_hdr));

    uint8_t info_hdr[40] = {0};
    info_hdr[0] = 40;
    info_hdr[4] = static_cast<uint8_t>(w & 0xFF);
    info_hdr[5] = static_cast<uint8_t>((w >> 8) & 0xFF);
    info_hdr[6] = static_cast<uint8_t>((w >> 16) & 0xFF);
    info_hdr[7] = static_cast<uint8_t>((w >> 24) & 0xFF);
    info_hdr[8] = static_cast<uint8_t>(h & 0xFF);
    info_hdr[9] = static_cast<uint8_t>((h >> 8) & 0xFF);
    info_hdr[10] = static_cast<uint8_t>((h >> 16) & 0xFF);
    info_hdr[11] = static_cast<uint8_t>((h >> 24) & 0xFF);
    info_hdr[12] = 1;
    info_hdr[14] = 24;
    info_hdr[16] = 0;
    info_hdr[20] = static_cast<uint8_t>(pixel_bytes & 0xFF);
    info_hdr[21] = static_cast<uint8_t>((pixel_bytes >> 8) & 0xFF);
    info_hdr[22] = static_cast<uint8_t>((pixel_bytes >> 16) & 0xFF);
    info_hdr[23] = static_cast<uint8_t>((pixel_bytes >> 24) & 0xFF);
    f.write(info_hdr, sizeof(info_hdr));

    uint8_t* rowbuf = static_cast<uint8_t*>(malloc(row24));
    if (!rowbuf)
    {
        f.close();
        set_error("SD buffer fail");
        return false;
    }
    memset(rowbuf, 0, row24);

    for (uint32_t y = 0; y < h; ++y)
    {
        const uint16_t* row = s_frame + (h - 1 - y) * w;
        uint32_t idx = 0;
        for (uint32_t x = 0; x < w; ++x)
        {
            uint16_t px = row[x];
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5) & 0x3F;
            uint8_t b5 = px & 0x1F;
            uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
            rowbuf[idx++] = b;
            rowbuf[idx++] = g;
            rowbuf[idx++] = r;
        }
        if (row24 > idx)
        {
            memset(rowbuf + idx, 0, row24 - idx);
        }
        f.write(rowbuf, row24);
    }

    f.flush();
    f.close();
    free(rowbuf);

    snprintf(s_saved_path, sizeof(s_saved_path), "%s", path);
    return true;
}

void set_status(sstv::State state, uint16_t line, float progress, float audio_level, bool has_image)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.line = line;
    s_status.progress = progress;
    s_status.audio_level = audio_level;
    s_status.has_image = has_image;
    portEXIT_CRITICAL(&s_lock);
}

void clear_accum()
{
    memset(s_accum, 0, sizeof(s_accum));
    memset(s_count, 0, sizeof(s_count));
}

void clear_robot_chroma()
{
    memset(s_last_ry, 128, sizeof(s_last_ry));
    memset(s_last_by, 128, sizeof(s_last_by));
    s_has_ry = false;
    s_has_by = false;
}

void clear_pd_state()
{
    memset(s_pd_y1, 0, sizeof(s_pd_y1));
    s_pd_has_y1 = false;
    s_pd_use_y1 = false;
}

void clear_frame()
{
    if (!s_frame)
    {
        return;
    }
    uint16_t bg = panel_rgb565();
    for (int y = 0; y < kOutHeight; ++y)
    {
        uint16_t* row = s_frame + (y * kOutWidth);
        for (int x = 0; x < kOutWidth; ++x)
        {
            row[x] = bg;
        }
    }
    s_last_output_y = -1;
}

uint8_t clamp_u8(int v)
{
    if (v < 0)
    {
        return 0;
    }
    if (v > 255)
    {
        return 255;
    }
    return static_cast<uint8_t>(v);
}

uint8_t freq_to_intensity(float freq)
{
    if (freq < kFreqMin)
    {
        freq = kFreqMin;
    }
    if (freq > kFreqMax)
    {
        freq = kFreqMax;
    }
    float ratio = (freq - kFreqMin) / kFreqSpan;
    int value = static_cast<int>(ratio * 255.0f + 0.5f);
    return clamp_u8(value);
}

void render_line(int line)
{
    if (!s_frame)
    {
        return;
    }
    int out_y = (line * kOutHeight) / s_line_count;
    if (out_y == s_last_output_y || out_y < 0 || out_y >= kOutHeight)
    {
        return;
    }
    s_last_output_y = out_y;

    uint16_t bg = panel_rgb565();
    uint16_t* row = s_frame + (out_y * kOutWidth);
    for (int x = 0; x < kOutWidth; ++x)
    {
        row[x] = bg;
    }

    for (int out_x = 0; out_x < kOutImageWidth; ++out_x)
    {
        int in_x = (out_x * kInWidth) / kOutImageWidth;
        if (in_x < 0)
        {
            in_x = 0;
        }
        if (in_x >= kInWidth)
        {
            in_x = kInWidth - 1;
        }

        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t r = 0;
    if (s_vis_mode == VisMode::Robot72 || s_vis_mode == VisMode::Robot36 ||
        s_vis_mode == VisMode::Pd50 || s_vis_mode == VisMode::Pd90 ||
        s_vis_mode == VisMode::Pd120 || s_vis_mode == VisMode::Pd160 ||
        s_vis_mode == VisMode::Pd180 || s_vis_mode == VisMode::Pd240 ||
        s_vis_mode == VisMode::Pd290)
    {
        int y = 0;
        if (s_vis_mode == VisMode::Pd50 || s_vis_mode == VisMode::Pd90 ||
            s_vis_mode == VisMode::Pd120 || s_vis_mode == VisMode::Pd160 ||
            s_vis_mode == VisMode::Pd180 || s_vis_mode == VisMode::Pd240 ||
            s_vis_mode == VisMode::Pd290)
        {
            if (s_pd_use_y1 && s_pd_has_y1)
            {
                y = s_pd_y1[in_x];
            }
            else
            {
                y = s_count[0][in_x] ? static_cast<int>(s_accum[0][in_x] / s_count[0][in_x]) : 0;
            }
        }
        else
        {
            y = s_count[0][in_x] ? static_cast<int>(s_accum[0][in_x] / s_count[0][in_x]) : 0;
        }
        int ry = 128;
        int by = 128;
        if (s_vis_mode == VisMode::Robot72)
        {
            ry = s_count[1][in_x] ? static_cast<int>(s_accum[1][in_x] / s_count[1][in_x]) : 128;
            by = s_count[2][in_x] ? static_cast<int>(s_accum[2][in_x] / s_count[2][in_x]) : 128;
        }
        else
        {
            ry = s_has_ry ? s_last_ry[in_x] : 128;
            by = s_has_by ? s_last_by[in_x] : 128;
        }
        int r_val = y + (ry - 128);
        int b_val = y + (by - 128);
        float g_val_f = (static_cast<float>(y) - 0.299f * r_val - 0.114f * b_val) / 0.587f;
        int g_val = static_cast<int>(g_val_f + 0.5f);
        r = clamp_u8(r_val);
            g = clamp_u8(g_val);
            b = clamp_u8(b_val);
        }
        else
        {
            if (s_count[0][in_x])
            {
                g = static_cast<uint8_t>(s_accum[0][in_x] / s_count[0][in_x]);
            }
            if (s_count[1][in_x])
            {
                b = static_cast<uint8_t>(s_accum[1][in_x] / s_count[1][in_x]);
            }
            if (s_count[2][in_x])
            {
                r = static_cast<uint8_t>(s_accum[2][in_x] / s_count[2][in_x]);
            }
        }

        row[kPadX + out_x] = rgb_to_565(r, g, b);
    }
}

void sstv_task(void*)
{
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board)
    {
        set_error("Board not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    if (!(board->getDevicesProbe() & HW_CODEC_ONLINE))
    {
        set_error("Audio codec not ready");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (board->codec.open(kBitsPerSample, kChannels, kSampleRate) != 0)
    {
        set_error("Codec open failed");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    s_codec_open = true;
    board->codec.setGain(kMicGainDb);
    board->codec.setMute(false);
    SSTV_LOG("[SSTV] codec open ok (rate=%lu, bits=%u, ch=%u)\n",
             static_cast<unsigned long>(kSampleRate), kBitsPerSample, kChannels);

    const int samples_per_block = 512;
    const int samples_per_read = samples_per_block * kChannels;
    int16_t* buffer = static_cast<int16_t*>(malloc(samples_per_read * sizeof(int16_t)));
    if (!buffer)
    {
        set_error("No audio buffer");
        set_status(sstv::State::Error, 0, 0.0f, 0.0f, false);
        s_codec_open = false;
        board->codec.close();
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    s_bin_1100 = make_bin(1100.0f);
    s_bin_1200 = make_bin(1200.0f);
    s_bin_1300 = make_bin(1300.0f);
    s_bin_1900 = make_bin(1900.0f);
    for (int i = 0; i < kPixelBinCount; ++i)
    {
        float freq = kFreqMin + static_cast<float>(i) * kPixelBinStep;
        s_pixel_bins[i] = make_bin(freq);
    }

    const int porch_samples = static_cast<int>(kSampleRate * (kPorchMs / 1000.0f));
    const int sync_samples = static_cast<int>(kSampleRate * (kSyncPulseMs / 1000.0f));
    const int robot_sync_samples = static_cast<int>(kSampleRate * (kRobotSyncPulseMs / 1000.0f));
    const int robot_sync_porch_samples =
        static_cast<int>(kSampleRate * (kRobotSyncPorchMs / 1000.0f));
    const int robot_sep_samples = static_cast<int>(kSampleRate * (kRobotSepMs / 1000.0f));
    const int robot_porch_samples = static_cast<int>(kSampleRate * (kRobotPorchMs / 1000.0f));
    const int martin_sync_samples = static_cast<int>(kSampleRate * (kMartinSyncMs / 1000.0f));
    const int martin_porch_samples = static_cast<int>(kSampleRate * (kMartinPorchMs / 1000.0f));
    const int pd_sync_samples = static_cast<int>(kSampleRate * (kPdSyncMs / 1000.0f));
    const int pd_porch_samples = static_cast<int>(kSampleRate * (kPdPorchMs / 1000.0f));
    int color_samples = 0;
    int pixel_window_samples = 0;
    float color_ms = kColorMsScottie1;
    update_color_timing(color_ms, color_samples, pixel_window_samples);
    int robot_y_samples = static_cast<int>(kSampleRate * (kRobotYMs / 1000.0f));
    int robot_chroma_samples = static_cast<int>(kSampleRate * (kRobotChromaMs / 1000.0f));
    int robot_pixel_window_y = calc_pixel_window_samples(robot_y_samples);
    int robot_pixel_window_c = calc_pixel_window_samples(robot_chroma_samples);
    int robot36_y_samples = static_cast<int>(kSampleRate * (kRobot36YMs / 1000.0f));
    int robot36_chroma_samples = static_cast<int>(kSampleRate * (kRobot36ChromaMs / 1000.0f));
    int robot36_pixel_window_y = calc_pixel_window_samples(robot36_y_samples);
    int robot36_pixel_window_c = calc_pixel_window_samples(robot36_chroma_samples);
    int pd_scan_samples = 0;
    int pd_pixel_window_samples = 0;
    int p_sync_samples = 0;
    int p_porch_samples = 0;
    int p_color_samples = 0;
    int p_pixel_window_samples = 0;
    const int min_sync_gap = static_cast<int>(kSampleRate * (kMinSyncGapMs / 1000.0f));
    const float header_window_ms =
        1000.0f * static_cast<float>(kHeaderHopSamples) / static_cast<float>(kSampleRate);
    const int vis_samples_per_bit =
        static_cast<int>(kSampleRate * (kVisBitMs / 1000.0f) + 0.5f);
    int leader_windows = static_cast<int>(kLeaderMs / header_window_ms + 0.5f);
    if (leader_windows < 1)
    {
        leader_windows = 1;
    }
    int break_windows = static_cast<int>(kBreakMs / header_window_ms + 0.5f);
    if (break_windows < 1)
    {
        break_windows = 1;
    }
    int vis_windows_per_bit = static_cast<int>(kVisBitMs / header_window_ms + 0.5f);
    if (vis_windows_per_bit < 1)
    {
        vis_windows_per_bit = 1;
    }

    int line_index = 0;
    int line_count = kInHeightScottie;
    s_line_count = line_count;
    Phase phase = Phase::Idle;
    int phase_samples = 0;
    int last_pixel = -1;
    int pixel_pos = 0;
    int pixel_fill = 0;

    HeaderState header_state = HeaderState::SeekLeader1;
    VisMode vis_mode = VisMode::Unknown;
    int header_count = 0;
    bool header_ok = false;
    int vis_bit_index = 0;
    uint8_t vis_value = 0;
    int vis_ones = 0;
    int vis_window_count = 0;
    int vis_1100_count = 0;
    int vis_1300_count = 0;
    int vis_valid_windows = 0;
    float vis_1100_sum = 0.0f;
    float vis_1300_sum = 0.0f;
    int vis_skip_windows = 0;
    int vis_start_count = 0;
    int vis_start_samples = 0;
    int vis_bit_samples = 0;
    int header_log_tick = 0;
    const int header_log_every = 10;
    int header_stat_tick = 0;
    const int header_stat_every = 50;
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
    int64_t vis_start_sample = -1;
    int64_t vis_last_bit_sample = -1;
    int vis_diag_bits = 0;
    double vis_diag_valid_sum = 0.0;
    double vis_diag_valid_min = 1.0;
    double vis_diag_valid_max = 0.0;
    float audio_level = 0.0f;

    int16_t header_buf[kHeaderWindowSamples] = {0};
    int header_pos = 0;
    int header_fill = 0;
    int header_hop = 0;
    int16_t sync_buf[kSyncWindowSamples] = {0};
    int sync_pos = 0;
    int sync_fill = 0;
    int sync_hop = 0;
    int16_t pixel_buf[kMaxPixelSamples] = {0};
    int16_t header_window[kHeaderWindowSamples];
    int16_t sync_window[kSyncWindowSamples];
    int16_t pixel_window[kMaxPixelSamples];

    int32_t mono = 0;
    ModeContext ctx = {
        vis_mode,
        phase,
        phase_samples,
        line_index,
        line_count,
        last_pixel,
        pixel_pos,
        pixel_fill,
        mono,
        audio_level,
        header_ok,
        pixel_buf,
        pixel_window,
        porch_samples,
        sync_samples,
        color_samples,
        pixel_window_samples,
        robot_sync_samples,
        robot_sync_porch_samples,
        robot_sep_samples,
        robot_porch_samples,
        robot_y_samples,
        robot_chroma_samples,
        robot36_y_samples,
        robot36_chroma_samples,
        robot_pixel_window_y,
        robot_pixel_window_c,
        robot36_pixel_window_y,
        robot36_pixel_window_c,
        martin_sync_samples,
        martin_porch_samples,
        pd_sync_samples,
        pd_porch_samples,
        pd_scan_samples,
        pd_pixel_window_samples,
        p_sync_samples,
        p_porch_samples,
        p_color_samples,
        p_pixel_window_samples,
        header_state,
        header_count,
        vis_bit_index,
        vis_value,
        vis_ones,
        vis_window_count,
        vis_1100_count,
        vis_1300_count,
        vis_valid_windows,
        vis_1100_sum,
        vis_1300_sum,
        vis_skip_windows,
        vis_start_count,
        vis_start_samples,
        vis_bit_samples,
        color_ms,
    };

    int64_t sample_index = 0;
    int64_t last_sync_index = -1000000;
    int64_t low_audio_samples = 0;

    clear_frame();
    clear_accum();
    clear_robot_chroma();
    clear_pd_state();
    set_status(sstv::State::Waiting, 0, 0.0f, 0.0f, false);

    while (!s_stop)
    {
        int read_state = board->codec.read(reinterpret_cast<uint8_t*>(buffer),
                                           samples_per_read * sizeof(int16_t));
        if (read_state != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(kTaskDelayMs));
            continue;
        }

        int16_t block_peak = 0;
        for (int i = 0; i < samples_per_block; ++i)
        {
            int32_t l = buffer[i * kChannels];
            int32_t r = (kChannels > 1) ? buffer[i * kChannels + 1] : l;
            mono = (l + r) / 2;
            if (mono > block_peak)
            {
                block_peak = mono;
            }
            if (mono < -block_peak)
            {
                block_peak = static_cast<int16_t>(-mono);
            }

            header_buf[header_pos] = static_cast<int16_t>(mono);
            header_pos++;
            if (header_pos >= kHeaderWindowSamples)
            {
                header_pos = 0;
            }
            if (header_fill < kHeaderWindowSamples)
            {
                header_fill++;
            }

            sync_buf[sync_pos] = static_cast<int16_t>(mono);
            sync_pos++;
            if (sync_pos >= kSyncWindowSamples)
            {
                sync_pos = 0;
            }
            if (sync_fill < kSyncWindowSamples)
            {
                sync_fill++;
            }

            if (!header_ok && header_fill == kHeaderWindowSamples)
            {
                header_hop++;
                if (header_hop >= kHeaderHopSamples)
                {
                    header_hop = 0;
                    for (int j = 0; j < kHeaderWindowSamples; ++j)
                    {
                        int idx = header_pos + j;
                        if (idx >= kHeaderWindowSamples)
                        {
                            idx -= kHeaderWindowSamples;
                        }
                        header_window[j] = header_buf[idx];
                    }

                    float p1100 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1100);
                    float p1200 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1200);
                    float p1300 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1300);
                    float p1900 = goertzel_power(header_window, kHeaderWindowSamples, s_bin_1900);
                    Tone tone = detect_tone(p1100, p1200, p1300, p1900);

                    if (header_state == HeaderState::ReadVisBits)
                    {
                        if (vis_skip_windows > 0)
                        {
                            vis_skip_windows--;
                        }
                        else
                        {
                            float total = p1100 + p1200 + p1300 + p1900;
                            float pair = p1100 + p1300;
                            if (total > 0.0f)
                            {
                                vis_1100_sum += p1100;
                                vis_1300_sum += p1300;
                                if (pair >= total * kVisPairRatio)
                                {
                                    vis_valid_windows++;
                                }
                            }
                            vis_window_count++;
                            vis_bit_samples += kHeaderHopSamples;

                            if (vis_bit_samples >= vis_samples_per_bit)
                            {
                                int bit = (vis_1100_sum >= vis_1300_sum) ? 1 : 0;
                                double valid_ratio = 0.0;
                                if (vis_window_count > 0)
                                {
                                    valid_ratio = static_cast<double>(vis_valid_windows) /
                                                  static_cast<double>(vis_window_count);
                                }
                                vis_diag_bits++;
                                vis_diag_valid_sum += valid_ratio;
                                if (valid_ratio < vis_diag_valid_min)
                                {
                                    vis_diag_valid_min = valid_ratio;
                                }
                                if (valid_ratio > vis_diag_valid_max)
                                {
                                    vis_diag_valid_max = valid_ratio;
                                }
                                const int64_t bit_sample = sample_index;
                                if (vis_last_bit_sample >= 0)
                                {
                                    const int64_t bit_len_samples = bit_sample - vis_last_bit_sample;
                                    const double bit_len_ms =
                                        static_cast<double>(bit_len_samples) * 1000.0 /
                                        static_cast<double>(kSampleRate);
                                    SSTV_LOG_VIS("[SSTV] VIS bit %d len=%.2fms valid=%.2f\n",
                                                 vis_bit_index, bit_len_ms, valid_ratio);
                                }
                                vis_last_bit_sample = bit_sample;
                                if (vis_1100_sum == 0.0f && vis_1300_sum == 0.0f)
                                {
                                    reset_header_state(ctx);
                                }
                                else
                                {
                                    SSTV_LOG_V("[SSTV] VIS window valid=%d total(1100=%.0f 1300=%.0f)\n",
                                               vis_valid_windows,
                                               static_cast<double>(vis_1100_sum),
                                               static_cast<double>(vis_1300_sum));
                                    if (vis_bit_index < 7)
                                    {
                                        if (bit)
                                        {
                                            vis_value |= static_cast<uint8_t>(1u << vis_bit_index);
                                            vis_ones++;
                                        }
                                        SSTV_LOG_V("[SSTV] VIS bit %d=%d\n", vis_bit_index, bit);
                                        vis_bit_index++;
                                    }
                                    else
                                    {
                                        int total_ones = vis_ones + (bit ? 1 : 0);
                                        bool parity_ok = (total_ones % 2) == 0;
                                        if (vis_start_sample >= 0 && vis_diag_bits > 0)
                                        {
                                            const double elapsed_ms =
                                                static_cast<double>(bit_sample - vis_start_sample) *
                                                1000.0 / static_cast<double>(kSampleRate);
                                            const double bit_ms = elapsed_ms / vis_diag_bits;
                                            const double drift_ppm =
                                                (bit_ms / kVisBitMs - 1.0) * 1000000.0;
                                            const double valid_avg =
                                                vis_diag_valid_sum / vis_diag_bits;
                                            SSTV_LOG_VIS("[SSTV] VIS timing bits=%d elapsed=%.2fms "
                                                         "bit=%.3fms drift=%.1fppm "
                                                         "valid(min/avg/max)=%.2f/%.2f/%.2f\n",
                                                         vis_diag_bits, elapsed_ms, bit_ms,
                                                         drift_ppm, vis_diag_valid_min,
                                                         valid_avg, vis_diag_valid_max);
                                        }
                                        SSTV_LOG("[SSTV] VIS value=%u parity=%d\n",
                                                 static_cast<unsigned>(vis_value), parity_ok ? 1 : 0);
                                        auto apply_vis_config = [&](const VisConfig* config,
                                                                    const char* label)
                                        {
                                            if (!config)
                                            {
                                                return false;
                                            }
                                            vis_mode = config->mode;
                                            color_ms = config->color_ms;
                                                if (config->pd_scan_ms > 0.0f)
                                                {
                                                    update_pd_timing(config->pd_scan_ms,
                                                                     pd_scan_samples,
                                                                     pd_pixel_window_samples);
                                                }
                                                if (config->p_sync_ms > 0.0f)
                                                {
                                                    update_p_timing(config->p_sync_ms,
                                                                    config->p_porch_ms,
                                                                    config->p_color_ms,
                                                                    p_sync_samples,
                                                                    p_porch_samples,
                                                                    p_color_samples,
                                                                    p_pixel_window_samples);
                                                }
                                                update_color_timing(color_ms, color_samples,
                                                                    pixel_window_samples);
                                            line_count = config->line_count;
                                            s_line_count = line_count;
                                            s_vis_mode = vis_mode;
                                            header_ok = true;
                                                SSTV_LOG("[SSTV] VIS ok: %s (%s, color=%.3f ms)\n",
                                                         vis_mode_name(vis_mode), label,
                                                         static_cast<double>(color_ms));
                                            return true;
                                        };

                                        bool accepted = false;
                                        if (parity_ok)
                                        {
                                            accepted = apply_vis_config(find_vis_config(vis_value),
                                                                        "normal");
                                        }

                                        if (!accepted)
                                        {
                                            uint8_t inv_value =
                                                static_cast<uint8_t>(~vis_value) & 0x7F;
                                            int inv_bit = bit ? 0 : 1;
                                            int inv_ones = 7 - vis_ones;
                                            int inv_total = inv_ones + (inv_bit ? 1 : 0);
                                            if ((inv_total % 2) == 0)
                                            {
                                                accepted = apply_vis_config(
                                                    find_vis_config(inv_value), "inv");
                                            }
                                        }

                                        if (!accepted)
                                        {
                                            uint8_t rev_value = reverse_vis_bits(vis_value);
                                            if (parity_ok)
                                            {
                                                accepted = apply_vis_config(
                                                    find_vis_config(rev_value), "rev");
                                            }
                                        }

                                        if (!accepted)
                                        {
                                            uint8_t rev_inv_value =
                                                reverse_vis_bits(static_cast<uint8_t>(~vis_value) &
                                                                 0x7F);
                                            int inv_bit = bit ? 0 : 1;
                                            int inv_ones = 7 - vis_ones;
                                            int inv_total = inv_ones + (inv_bit ? 1 : 0);
                                            if ((inv_total % 2) == 0)
                                            {
                                                accepted = apply_vis_config(
                                                    find_vis_config(rev_inv_value), "rev+inv");
                                            }
                                        }

                                        if (!accepted)
                                        {
                                            SSTV_LOG_V("[SSTV] VIS unsupported value=%u\n",
                                                       static_cast<unsigned>(vis_value));
                                        }
                                        reset_header_state(ctx);
                                    }
                                }

                                if (vis_bit_samples >= vis_samples_per_bit)
                                {
                                    vis_bit_samples -= vis_samples_per_bit;
                                }
                                vis_window_count = 0;
                                vis_1100_count = 0;
                                vis_1300_count = 0;
                                vis_valid_windows = 0;
                                vis_1100_sum = 0.0f;
                                vis_1300_sum = 0.0f;
                            }
                        }
                    }
                    else
                    {
                        if ((header_state == HeaderState::SeekBreak ||
                             header_state == HeaderState::SeekLeader2 ||
                             header_state == HeaderState::SeekVisStart) &&
                            (header_log_tick++ % header_log_every == 0))
                        {
                            SSTV_LOG_V("[SSTV] hdr state=%s tone=%s p1100=%.0f p1200=%.0f p1300=%.0f p1900=%.0f\n",
                                       header_state_name(header_state), tone_name(tone),
                                       static_cast<double>(p1100), static_cast<double>(p1200),
                                       static_cast<double>(p1300), static_cast<double>(p1900));
                        }

                        switch (header_state)
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
                                header_count++;
                                if (header_count >= leader_windows)
                                {
                                    header_state = HeaderState::SeekBreak;
                                    header_count = 0;
                                    SSTV_LOG("[SSTV] header leader1 ok\n");
                                }
                            }
                            else
                            {
                                header_count = 0;
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
                            break_window_count++;
                            break_ratio_total_sum += ratio_total;
                            break_ratio_max_sum += ratio_max;
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
                                else if (header_log_tick % header_log_every == 0)
                                {
                                    SSTV_LOG_V("[SSTV] break miss r_total=%.3f r_max=%.3f\n",
                                               static_cast<double>(ratio_total),
                                               static_cast<double>(ratio_max));
                                }
                            }
                            if (break_hit)
                            {
                                break_hit_count++;
                                header_count++;
                                if (header_count >= break_windows)
                                {
                                    header_state = HeaderState::SeekLeader2;
                                    header_count = 0;
                                    SSTV_LOG("[SSTV] header break ok\n");
                                }
                            }
                            else if (tone == Tone::Tone1900)
                            {
                                header_state = HeaderState::SeekLeader1;
                                header_count = 1;
                            }
                            else
                            {
                                header_count = 0;
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
                            leader2_window_count++;
                            leader2_ratio_total_sum += ratio_total;
                            leader2_ratio_max_sum += ratio_max;
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
                                header_count++;
                                leader2_hit_count++;
                                if (header_count >= leader_windows)
                                {
                                    header_state = HeaderState::SeekVisStart;
                                    header_count = 0;
                                    vis_start_count = 0;
                                    SSTV_LOG("[SSTV] header leader2 ok\n");
                                }
                            }
                            else
                            {
                                header_state = HeaderState::SeekLeader1;
                                header_count = 0;
                            }
                            if (!leader2_hit && header_log_tick % header_log_every == 0)
                            {
                                SSTV_LOG_V("[SSTV] leader2 miss r_total=%.3f r_max=%.3f\n",
                                           static_cast<double>(ratio_total),
                                           static_cast<double>(ratio_max));
                            }
                            break;
                        }
                        case HeaderState::SeekVisStart:
                        {
                            bool vis_start = (tone == Tone::Tone1200);
                            float total = p1100 + p1200 + p1300 + p1900;
                            float max_other = p1300 > p1900 ? p1300 : p1900;
                            float ratio_total = total > 0.0f ? p1200 / total : 0.0f;
                            float ratio_max = max_other > 0.0f ? p1200 / max_other : 0.0f;
                            vis_stat_window_count++;
                            vis_ratio_total_sum += ratio_total;
                            vis_ratio_max_sum += ratio_max;
                            if (!vis_start)
                            {
                                vis_start = (total > 0.0f &&
                                             p1200 > total * 0.0002f &&
                                             p1200 > max_other * 0.0004f);
                                if (vis_start)
                                {
                                    SSTV_LOG_V("[SSTV] visstart fallback p1200=%.0f p1300=%.0f p1900=%.0f\n",
                                               static_cast<double>(p1200),
                                               static_cast<double>(p1300),
                                               static_cast<double>(p1900));
                                }
                                else if (header_log_tick % header_log_every == 0)
                                {
                                    SSTV_LOG_V("[SSTV] visstart miss r_total=%.3f r_max=%.3f\n",
                                               static_cast<double>(ratio_total),
                                               static_cast<double>(ratio_max));
                                }
                            }
                            if (vis_start)
                            {
                                vis_start_count++;
                                vis_start_samples += kHeaderHopSamples;
                                if (vis_start_samples >= vis_samples_per_bit)
                                {
                                    vis_hit_count++;
                                    header_state = HeaderState::ReadVisBits;
                                    vis_skip_windows = 0;
                                    vis_bit_index = 0;
                                    vis_value = 0;
                                    vis_ones = 0;
                                    vis_window_count = 0;
                                    vis_1100_count = 0;
                                    vis_1300_count = 0;
                                    vis_valid_windows = 0;
                                    vis_1100_sum = 0.0f;
                                    vis_1300_sum = 0.0f;
                                    vis_start_count = 0;
                                    vis_start_samples = 0;
                                    vis_bit_samples = 0;
                                    vis_start_sample = sample_index;
                                    vis_last_bit_sample = -1;
                                    vis_diag_bits = 0;
                                    vis_diag_valid_sum = 0.0;
                                    vis_diag_valid_min = 1.0;
                                    vis_diag_valid_max = 0.0;
                                    SSTV_LOG("[SSTV] header VIS start\n");
                                    if (vis_samples_per_bit > 0)
                                    {
                                        const int start_phase =
                                            static_cast<int>(vis_start_sample % vis_samples_per_bit);
                                        const double start_phase_ms =
                                            static_cast<double>(start_phase) * 1000.0 /
                                            static_cast<double>(kSampleRate);
                                        const double window_bit_ms =
                                            static_cast<double>(vis_samples_per_bit) * 1000.0 /
                                            static_cast<double>(kSampleRate);
                                        SSTV_LOG_VIS("[SSTV] VIS align sample=%lld phase=%.2fms "
                                                     "hop=%.3fms bit=%.3fms windows/bit=%d "
                                                     "window_bit=%.3fms err=%.3fms\n",
                                                     static_cast<long long>(vis_start_sample),
                                                     start_phase_ms, header_window_ms, kVisBitMs,
                                                     vis_windows_per_bit, window_bit_ms,
                                                     window_bit_ms - kVisBitMs);
                                    }
                                }
                            }
                            else if (tone == Tone::Tone1900)
                            {
                                vis_start_count = 0;
                                vis_start_samples = 0;
                                header_state = HeaderState::SeekLeader2;
                                header_count = 1;
                            }
                            else
                            {
                                vis_start_count = 0;
                                vis_start_samples = 0;
                            }
                            break;
                        }
                        case HeaderState::ReadVisBits:
                            break;
                        }

                        if (header_stat_tick++ % header_stat_every == 0)
                        {
                            double break_avg_total =
                                break_window_count > 0 ? break_ratio_total_sum / break_window_count : 0.0;
                            double break_avg_max =
                                break_window_count > 0 ? break_ratio_max_sum / break_window_count : 0.0;
                            double leader2_avg_total = leader2_window_count > 0
                                                           ? leader2_ratio_total_sum / leader2_window_count
                                                           : 0.0;
                            double leader2_avg_max = leader2_window_count > 0
                                                         ? leader2_ratio_max_sum / leader2_window_count
                                                         : 0.0;
                            double vis_avg_total = vis_stat_window_count > 0
                                                       ? vis_ratio_total_sum / vis_stat_window_count
                                                       : 0.0;
                            double vis_avg_max = vis_stat_window_count > 0
                                                     ? vis_ratio_max_sum / vis_stat_window_count
                                                     : 0.0;
                            SSTV_LOG_V("[SSTV] stat break=%d/%d avg(%.3f,%.3f) leader2=%d/%d avg(%.3f,%.3f) vis=%d/%d avg(%.3f,%.3f)\n",
                                       break_hit_count, break_window_count, break_avg_total,
                                       break_avg_max, leader2_hit_count, leader2_window_count,
                                       leader2_avg_total, leader2_avg_max, vis_hit_count,
                                       vis_stat_window_count, vis_avg_total, vis_avg_max);
                        }
                    }
                }
            }

            bool can_sync = header_ok || s_status.state == sstv::State::Receiving;
            if (can_sync && sync_fill == kSyncWindowSamples)
            {
                sync_hop++;
                if (sync_hop >= kSyncHopSamples)
                {
                    sync_hop = 0;
                    for (int j = 0; j < kSyncWindowSamples; ++j)
                    {
                        int idx = sync_pos + j;
                        if (idx >= kSyncWindowSamples)
                        {
                            idx -= kSyncWindowSamples;
                        }
                        sync_window[j] = sync_buf[idx];
                    }

                    float p1100 = goertzel_power(sync_window, kSyncWindowSamples, s_bin_1100);
                    float p1200 = goertzel_power(sync_window, kSyncWindowSamples, s_bin_1200);
                    float p1300 = goertzel_power(sync_window, kSyncWindowSamples, s_bin_1300);
                    float total = p1100 + p1200 + p1300;
                    float other_max = p1100 > p1300 ? p1100 : p1300;
                    bool sync_hit = (p1200 > other_max * kSyncToneDetectRatio &&
                                     p1200 > total * kSyncToneTotalRatio);

                    if (sync_hit && sample_index - last_sync_index > min_sync_gap)
                    {
                        last_sync_index = sample_index;
                        SSTV_LOG_V("[SSTV] sync hit @%lld line=%d state=%d\n",
                                   static_cast<long long>(sample_index), line_index,
                                   static_cast<int>(s_status.state));
                        if (s_status.state != sstv::State::Receiving)
                        {
                            line_index = 0;
                            clear_frame();
                            clear_saved_path();
                            s_pending_save = false;
                        }
                        const bool robot_mode = (vis_mode == VisMode::Robot72 ||
                                                 vis_mode == VisMode::Robot36);
                        const bool martin_mode = (vis_mode == VisMode::Martin1 ||
                                                  vis_mode == VisMode::Martin2);
                        const bool pd_mode = (vis_mode == VisMode::Pd50 ||
                                              vis_mode == VisMode::Pd90 ||
                                              vis_mode == VisMode::Pd120 ||
                                              vis_mode == VisMode::Pd160 ||
                                              vis_mode == VisMode::Pd180 ||
                                              vis_mode == VisMode::Pd240 ||
                                              vis_mode == VisMode::Pd290);
                        const bool p_mode = (vis_mode == VisMode::P3 ||
                                             vis_mode == VisMode::P5 ||
                                             vis_mode == VisMode::P7);
                        if (s_status.state != sstv::State::Receiving)
                        {
                            clear_accum();
                            if (robot_mode)
                            {
                                phase = Phase::RobotSync;
                            }
                            else if (martin_mode)
                            {
                                phase = Phase::MartinSync;
                            }
                            else if (pd_mode)
                            {
                                phase = Phase::PdSync;
                            }
                            else if (p_mode)
                            {
                                phase = Phase::PSync;
                            }
                            else
                            {
                                phase = Phase::Porch1;
                            }
                            phase_samples = 0;
                            reset_pixel_state(ctx);
                            clear_pd_state();
                            set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                                       static_cast<float>(line_index) / line_count,
                                       audio_level, true);
                        }
                        else if (martin_mode)
                        {
                            clear_accum();
                            phase = Phase::MartinSync;
                            phase_samples = 0;
                            reset_pixel_state(ctx);
                        }
                        else if (pd_mode)
                        {
                            if (phase == Phase::PdSync)
                            {
                                last_sync_index = sample_index;
                            }
                        }
                        else if (p_mode)
                        {
                            clear_accum();
                            phase = Phase::PSync;
                            phase_samples = 0;
                            reset_pixel_state(ctx);
                        }
                        else if (!robot_mode && phase == Phase::Blue)
                        {
                            phase = Phase::Sync;
                            phase_samples = 0;
                        }
                    }
                }
            }

            if (s_status.state == sstv::State::Receiving && phase != Phase::Idle)
            {
                const ModeStrategy& strategy = select_mode_strategy(vis_mode);
                strategy.step(ctx);
            }

            sample_index++;
        }

        audio_level = (audio_level * 0.8f) +
                      (static_cast<float>(block_peak) / 32767.0f) * 0.2f;
        if (audio_level < 0.0f)
        {
            audio_level = 0.0f;
        }
        if (audio_level > 1.0f)
        {
            audio_level = 1.0f;
        }
        if (s_pending_save)
        {
            save_frame_to_sd();
            s_pending_save = false;
        }

        if (audio_level < kAudioResetLevel)
        {
            low_audio_samples += samples_per_block;
        }
        else
        {
            low_audio_samples = 0;
        }

        if (s_status.state == sstv::State::Receiving)
        {
            const int64_t sync_timeout_samples =
                static_cast<int64_t>(kSampleRate * (kSyncTimeoutMs / 1000.0f));
            const int64_t audio_timeout_samples =
                static_cast<int64_t>(kSampleRate * (kAudioResetHoldMs / 1000.0f));
            bool should_reset = false;
            if (sample_index - last_sync_index > sync_timeout_samples)
            {
                SSTV_LOG("[SSTV] reset: sync timeout %.0f ms\n",
                         static_cast<double>((sample_index - last_sync_index) * 1000.0 /
                                             static_cast<double>(kSampleRate)));
                should_reset = true;
            }
            else if (low_audio_samples > audio_timeout_samples)
            {
                SSTV_LOG("[SSTV] reset: low audio %.0f ms (level=%.3f)\n",
                         static_cast<double>(low_audio_samples * 1000.0 /
                                             static_cast<double>(kSampleRate)),
                         static_cast<double>(audio_level));
                should_reset = true;
            }

            if (should_reset)
            {
                header_ok = false;
                reset_header_state(ctx);
                reset_pixel_state(ctx);
                phase = Phase::Idle;
                phase_samples = 0;
                line_index = 0;
                clear_accum();
                s_pending_save = false;
                set_status(sstv::State::Waiting, 0, 0.0f, audio_level, s_frame != nullptr);
            }
        }

        if (s_status.state == sstv::State::Waiting)
        {
            set_status(sstv::State::Waiting, 0, 0.0f, audio_level, s_frame != nullptr);
        }
        else if (s_status.state == sstv::State::Receiving)
        {
            set_status(sstv::State::Receiving, static_cast<uint16_t>(line_index),
                       static_cast<float>(line_index) / line_count, audio_level, true);
        }
        else if (s_status.state == sstv::State::Complete)
        {
            set_status(sstv::State::Complete, static_cast<uint16_t>(line_count),
                       1.0f, audio_level, true);
        }
    }

    free(buffer);
    s_task = nullptr;
    if (s_codec_open)
    {
        board->codec.close();
        s_codec_open = false;
    }
    set_status(sstv::State::Idle, 0, 0.0f, 0.0f, s_frame != nullptr);
    vTaskDelete(nullptr);
}

void step_robot_mode(ModeContext& ctx)
{
    const bool robot36 = (ctx.vis_mode == VisMode::Robot36);
    const bool even_line = (ctx.line_index % 2) == 0;
    if (ctx.phase == Phase::RobotSync)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.robot_sync_samples)
        {
            ctx.phase = Phase::RobotPorch1;
            ctx.phase_samples = 0;
        }
    }
    else if (ctx.phase == Phase::RobotPorch1)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.robot_sync_porch_samples)
        {
            ctx.phase = Phase::RobotY;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::RobotSep1)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.robot_sep_samples)
        {
            ctx.phase = Phase::RobotPorch2;
            ctx.phase_samples = 0;
        }
    }
    else if (ctx.phase == Phase::RobotPorch2)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.robot_porch_samples)
        {
            if (robot36)
            {
                ctx.phase = even_line ? Phase::RobotRY : Phase::RobotBY;
            }
            else
            {
                ctx.phase = Phase::RobotRY;
            }
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (!robot36 && ctx.phase == Phase::RobotSep2)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.robot_sep_samples)
        {
            ctx.phase = Phase::RobotPorch3;
            ctx.phase_samples = 0;
        }
    }
    else if (!robot36 && ctx.phase == Phase::RobotPorch3)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.robot_porch_samples)
        {
            ctx.phase = Phase::RobotBY;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else
    {
        const bool y_phase = (ctx.phase == Phase::RobotY);
        const int active_color_samples =
            y_phase ? (robot36 ? ctx.robot36_y_samples : ctx.robot_y_samples)
                    : (robot36 ? ctx.robot36_chroma_samples : ctx.robot_chroma_samples);
        const int active_pixel_window =
            y_phase ? (robot36 ? ctx.robot36_pixel_window_y : ctx.robot_pixel_window_y)
                    : (robot36 ? ctx.robot36_pixel_window_c : ctx.robot_pixel_window_c);
        int pixel = (ctx.phase_samples * kInWidth) / active_color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            ctx.pixel_buf[ctx.pixel_pos] = static_cast<int16_t>(ctx.mono);
            ctx.pixel_pos++;
            if (ctx.pixel_pos >= active_pixel_window)
            {
                ctx.pixel_pos = 0;
            }
            if (ctx.pixel_fill < active_pixel_window)
            {
                ctx.pixel_fill++;
            }

            if (pixel != ctx.last_pixel && ctx.pixel_fill == active_pixel_window)
            {
                ctx.last_pixel = pixel;
                for (int j = 0; j < active_pixel_window; ++j)
                {
                    int idx = ctx.pixel_pos + j;
                    if (idx >= active_pixel_window)
                    {
                        idx -= active_pixel_window;
                    }
                    ctx.pixel_window[j] = ctx.pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(ctx.pixel_window, active_pixel_window,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (ctx.phase == Phase::RobotY)
                {
                    channel = 0;
                }
                else if (ctx.phase == Phase::RobotRY)
                {
                    channel = 1;
                }
                else if (ctx.phase == Phase::RobotBY)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        ctx.phase_samples++;
        if (ctx.phase_samples >= active_color_samples)
        {
            ctx.phase_samples = 0;
            if (ctx.phase == Phase::RobotY)
            {
                ctx.phase = Phase::RobotSep1;
            }
            else if (ctx.phase == Phase::RobotRY)
            {
                if (robot36)
                {
                    for (int x = 0; x < kInWidth; ++x)
                    {
                        if (s_count[1][x])
                        {
                            s_last_ry[x] = static_cast<uint8_t>(s_accum[1][x] / s_count[1][x]);
                        }
                    }
                    s_has_ry = true;
                    render_line(ctx.line_index);
                    SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
                    ctx.line_index++;
                    clear_accum();
                    if (ctx.line_index >= ctx.line_count)
                    {
                        set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count),
                                   1.0f, ctx.audio_level, true);
                        s_pending_save = true;
                        ctx.phase = Phase::Idle;
                        ctx.header_ok = false;
                        reset_header_state(ctx);
                        SSTV_LOG("[SSTV] complete\n");
                    }
                    else
                    {
                        set_status(sstv::State::Receiving, static_cast<uint16_t>(ctx.line_index),
                                   static_cast<float>(ctx.line_index) / ctx.line_count,
                                   ctx.audio_level, true);
                        ctx.phase = Phase::RobotSync;
                        ctx.phase_samples = 0;
                        reset_pixel_state(ctx);
                    }
                }
                else
                {
                    ctx.phase = Phase::RobotSep2;
                }
            }
            else if (ctx.phase == Phase::RobotBY)
            {
                if (robot36)
                {
                    for (int x = 0; x < kInWidth; ++x)
                    {
                        if (s_count[2][x])
                        {
                            s_last_by[x] = static_cast<uint8_t>(s_accum[2][x] / s_count[2][x]);
                        }
                    }
                    s_has_by = true;
                }
                render_line(ctx.line_index);
                SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
                ctx.line_index++;
                clear_accum();
                if (ctx.line_index >= ctx.line_count)
                {
                    set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count), 1.0f,
                               ctx.audio_level, true);
                    s_pending_save = true;
                    ctx.phase = Phase::Idle;
                    ctx.header_ok = false;
                    reset_header_state(ctx);
                    SSTV_LOG("[SSTV] complete\n");
                }
                else
                {
                    set_status(sstv::State::Receiving, static_cast<uint16_t>(ctx.line_index),
                               static_cast<float>(ctx.line_index) / ctx.line_count,
                               ctx.audio_level, true);
                    ctx.phase = Phase::RobotSync;
                    ctx.phase_samples = 0;
                    reset_pixel_state(ctx);
                }
            }
        }
    }
}

void step_martin_mode(ModeContext& ctx)
{
    if (ctx.phase == Phase::MartinSync)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.martin_sync_samples)
        {
            ctx.phase = Phase::MartinPorch;
            ctx.phase_samples = 0;
        }
    }
    else if (ctx.phase == Phase::MartinPorch)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.martin_porch_samples)
        {
            ctx.phase = Phase::MartinGreen;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::MartinSep1)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.martin_porch_samples)
        {
            ctx.phase = Phase::MartinBlue;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::MartinSep2)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.martin_porch_samples)
        {
            ctx.phase = Phase::MartinRed;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::MartinSep3)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.martin_porch_samples)
        {
            ctx.phase = Phase::MartinSync;
            ctx.phase_samples = 0;
        }
    }
    else
    {
        int pixel = (ctx.phase_samples * kInWidth) / ctx.color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            ctx.pixel_buf[ctx.pixel_pos] = static_cast<int16_t>(ctx.mono);
            ctx.pixel_pos++;
            if (ctx.pixel_pos >= ctx.pixel_window_samples)
            {
                ctx.pixel_pos = 0;
            }
            if (ctx.pixel_fill < ctx.pixel_window_samples)
            {
                ctx.pixel_fill++;
            }

            if (pixel != ctx.last_pixel && ctx.pixel_fill == ctx.pixel_window_samples)
            {
                ctx.last_pixel = pixel;
                for (int j = 0; j < ctx.pixel_window_samples; ++j)
                {
                    int idx = ctx.pixel_pos + j;
                    if (idx >= ctx.pixel_window_samples)
                    {
                        idx -= ctx.pixel_window_samples;
                    }
                    ctx.pixel_window[j] = ctx.pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(ctx.pixel_window, ctx.pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (ctx.phase == Phase::MartinGreen)
                {
                    channel = 0;
                }
                else if (ctx.phase == Phase::MartinBlue)
                {
                    channel = 1;
                }
                else if (ctx.phase == Phase::MartinRed)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.color_samples)
        {
            ctx.phase_samples = 0;
            if (ctx.phase == Phase::MartinGreen)
            {
                ctx.phase = Phase::MartinSep1;
            }
            else if (ctx.phase == Phase::MartinBlue)
            {
                ctx.phase = Phase::MartinSep2;
            }
            else if (ctx.phase == Phase::MartinRed)
            {
                render_line(ctx.line_index);
                SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
                ctx.line_index++;
                clear_accum();
                if (ctx.line_index >= ctx.line_count)
                {
                    set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count), 1.0f,
                               ctx.audio_level, true);
                    s_pending_save = true;
                    ctx.phase = Phase::Idle;
                    ctx.header_ok = false;
                    reset_header_state(ctx);
                    SSTV_LOG("[SSTV] complete\n");
                }
                else
                {
                    set_status(sstv::State::Receiving, static_cast<uint16_t>(ctx.line_index),
                               static_cast<float>(ctx.line_index) / ctx.line_count,
                               ctx.audio_level, true);
                    ctx.phase = Phase::MartinSep3;
                    ctx.phase_samples = 0;
                    reset_pixel_state(ctx);
                }
            }
        }
    }
}

void step_pd_mode(ModeContext& ctx)
{
    if (ctx.phase == Phase::PdSync)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.pd_sync_samples)
        {
            ctx.phase = Phase::PdPorch;
            ctx.phase_samples = 0;
        }
    }
    else if (ctx.phase == Phase::PdPorch)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.pd_porch_samples)
        {
            ctx.phase = Phase::PdY1;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else
    {
        const bool y_phase = (ctx.phase == Phase::PdY1 || ctx.phase == Phase::PdY2);
        int pixel = (ctx.phase_samples * kInWidth) / ctx.pd_scan_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            ctx.pixel_buf[ctx.pixel_pos] = static_cast<int16_t>(ctx.mono);
            ctx.pixel_pos++;
            if (ctx.pixel_pos >= ctx.pd_pixel_window_samples)
            {
                ctx.pixel_pos = 0;
            }
            if (ctx.pixel_fill < ctx.pd_pixel_window_samples)
            {
                ctx.pixel_fill++;
            }

            if (pixel != ctx.last_pixel && ctx.pixel_fill == ctx.pd_pixel_window_samples)
            {
                ctx.last_pixel = pixel;
                for (int j = 0; j < ctx.pd_pixel_window_samples; ++j)
                {
                    int idx = ctx.pixel_pos + j;
                    if (idx >= ctx.pd_pixel_window_samples)
                    {
                        idx -= ctx.pd_pixel_window_samples;
                    }
                    ctx.pixel_window[j] = ctx.pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(ctx.pixel_window, ctx.pd_pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (y_phase)
                {
                    channel = 0;
                }
                else if (ctx.phase == Phase::PdRY)
                {
                    channel = 1;
                }
                else if (ctx.phase == Phase::PdBY)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.pd_scan_samples)
        {
            ctx.phase_samples = 0;
            if (ctx.phase == Phase::PdY1)
            {
                for (int x = 0; x < kInWidth; ++x)
                {
                    s_pd_y1[x] = s_count[0][x]
                                     ? static_cast<uint8_t>(s_accum[0][x] / s_count[0][x])
                                     : 0;
                }
                s_pd_has_y1 = true;
                clear_accum();
                ctx.phase = Phase::PdRY;
                reset_pixel_state(ctx);
            }
            else if (ctx.phase == Phase::PdRY)
            {
                for (int x = 0; x < kInWidth; ++x)
                {
                    s_last_ry[x] = s_count[1][x]
                                       ? static_cast<uint8_t>(s_accum[1][x] / s_count[1][x])
                                       : 128;
                }
                s_has_ry = true;
                clear_accum();
                ctx.phase = Phase::PdBY;
                reset_pixel_state(ctx);
            }
            else if (ctx.phase == Phase::PdBY)
            {
                for (int x = 0; x < kInWidth; ++x)
                {
                    s_last_by[x] = s_count[2][x]
                                       ? static_cast<uint8_t>(s_accum[2][x] / s_count[2][x])
                                       : 128;
                }
                s_has_by = true;
                clear_accum();

                if (s_pd_has_y1)
                {
                    s_pd_use_y1 = true;
                    render_line(ctx.line_index);
                    SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
                    ctx.line_index++;
                    if (ctx.line_index >= ctx.line_count)
                    {
                        set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count),
                                   1.0f, ctx.audio_level, true);
                        s_pending_save = true;
                        ctx.phase = Phase::Idle;
                        ctx.header_ok = false;
                        reset_header_state(ctx);
                        SSTV_LOG("[SSTV] complete\n");
                        return;
                    }
                }

                ctx.phase = Phase::PdY2;
                reset_pixel_state(ctx);
            }
            else if (ctx.phase == Phase::PdY2)
            {
                s_pd_use_y1 = false;
                render_line(ctx.line_index);
                SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
                ctx.line_index++;
                clear_accum();
                s_pd_has_y1 = false;
                if (ctx.line_index >= ctx.line_count)
                {
                    set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count), 1.0f,
                               ctx.audio_level, true);
                    s_pending_save = true;
                    ctx.phase = Phase::Idle;
                    ctx.header_ok = false;
                    reset_header_state(ctx);
                    SSTV_LOG("[SSTV] complete\n");
                }
                else
                {
                    set_status(sstv::State::Receiving, static_cast<uint16_t>(ctx.line_index),
                               static_cast<float>(ctx.line_index) / ctx.line_count,
                               ctx.audio_level, true);
                    ctx.phase = Phase::PdSync;
                    ctx.phase_samples = 0;
                    reset_pixel_state(ctx);
                }
            }
        }
    }
}

void step_p_mode(ModeContext& ctx)
{
    if (ctx.phase == Phase::PSync)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.p_sync_samples)
        {
            ctx.phase = Phase::PPorch1;
            ctx.phase_samples = 0;
        }
    }
    else if (ctx.phase == Phase::PPorch1)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.p_porch_samples)
        {
            ctx.phase = Phase::PRed;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::PPorch2)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.p_porch_samples)
        {
            ctx.phase = Phase::PGreen;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::PPorch3)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.p_porch_samples)
        {
            ctx.phase = Phase::PBlue;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::PPorch4)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.p_porch_samples)
        {
            render_line(ctx.line_index);
            SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
            ctx.line_index++;
            clear_accum();
            if (ctx.line_index >= ctx.line_count)
            {
                set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count), 1.0f,
                           ctx.audio_level, true);
                s_pending_save = true;
                ctx.phase = Phase::Idle;
                ctx.header_ok = false;
                reset_header_state(ctx);
                SSTV_LOG("[SSTV] complete\n");
            }
            else
            {
                set_status(sstv::State::Receiving, static_cast<uint16_t>(ctx.line_index),
                           static_cast<float>(ctx.line_index) / ctx.line_count, ctx.audio_level,
                           true);
                ctx.phase = Phase::PSync;
                ctx.phase_samples = 0;
                reset_pixel_state(ctx);
            }
        }
    }
    else
    {
        int pixel = (ctx.phase_samples * kInWidth) / ctx.p_color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            ctx.pixel_buf[ctx.pixel_pos] = static_cast<int16_t>(ctx.mono);
            ctx.pixel_pos++;
            if (ctx.pixel_pos >= ctx.p_pixel_window_samples)
            {
                ctx.pixel_pos = 0;
            }
            if (ctx.pixel_fill < ctx.p_pixel_window_samples)
            {
                ctx.pixel_fill++;
            }

            if (pixel != ctx.last_pixel && ctx.pixel_fill == ctx.p_pixel_window_samples)
            {
                ctx.last_pixel = pixel;
                for (int j = 0; j < ctx.p_pixel_window_samples; ++j)
                {
                    int idx = ctx.pixel_pos + j;
                    if (idx >= ctx.p_pixel_window_samples)
                    {
                        idx -= ctx.p_pixel_window_samples;
                    }
                    ctx.pixel_window[j] = ctx.pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(ctx.pixel_window, ctx.p_pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (ctx.phase == Phase::PRed)
                {
                    channel = 2;
                }
                else if (ctx.phase == Phase::PGreen)
                {
                    channel = 0;
                }
                else if (ctx.phase == Phase::PBlue)
                {
                    channel = 1;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.p_color_samples)
        {
            ctx.phase_samples = 0;
            if (ctx.phase == Phase::PRed)
            {
                ctx.phase = Phase::PPorch2;
            }
            else if (ctx.phase == Phase::PGreen)
            {
                ctx.phase = Phase::PPorch3;
            }
            else if (ctx.phase == Phase::PBlue)
            {
                ctx.phase = Phase::PPorch4;
            }
        }
    }
}

void step_scottie_mode(ModeContext& ctx)
{
    if (ctx.phase == Phase::Porch1)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.porch_samples)
        {
            ctx.phase = Phase::Green;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::Porch2)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.porch_samples)
        {
            ctx.phase = Phase::Blue;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else if (ctx.phase == Phase::Sync)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.sync_samples)
        {
            ctx.phase = Phase::Porch3;
            ctx.phase_samples = 0;
        }
    }
    else if (ctx.phase == Phase::Porch3)
    {
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.porch_samples)
        {
            ctx.phase = Phase::Red;
            ctx.phase_samples = 0;
            reset_pixel_state(ctx);
        }
    }
    else
    {
        int pixel = (ctx.phase_samples * kInWidth) / ctx.color_samples;
        if (pixel >= 0 && pixel < kInWidth)
        {
            ctx.pixel_buf[ctx.pixel_pos] = static_cast<int16_t>(ctx.mono);
            ctx.pixel_pos++;
            if (ctx.pixel_pos >= ctx.pixel_window_samples)
            {
                ctx.pixel_pos = 0;
            }
            if (ctx.pixel_fill < ctx.pixel_window_samples)
            {
                ctx.pixel_fill++;
            }

            if (pixel != ctx.last_pixel && ctx.pixel_fill == ctx.pixel_window_samples)
            {
                ctx.last_pixel = pixel;
                for (int j = 0; j < ctx.pixel_window_samples; ++j)
                {
                    int idx = ctx.pixel_pos + j;
                    if (idx >= ctx.pixel_window_samples)
                    {
                        idx -= ctx.pixel_window_samples;
                    }
                    ctx.pixel_window[j] = ctx.pixel_buf[idx];
                }

                float freq = estimate_freq_from_bins(ctx.pixel_window, ctx.pixel_window_samples,
                                                     s_pixel_bins, kPixelBinCount);
                uint8_t intensity = freq_to_intensity(freq);
                int channel = 0;
                if (ctx.phase == Phase::Green)
                {
                    channel = 0;
                }
                else if (ctx.phase == Phase::Blue)
                {
                    channel = 1;
                }
                else if (ctx.phase == Phase::Red)
                {
                    channel = 2;
                }
                s_accum[channel][pixel] += intensity;
                s_count[channel][pixel] += 1;
            }
        }
        ctx.phase_samples++;
        if (ctx.phase_samples >= ctx.color_samples)
        {
            ctx.phase_samples = 0;
            if (ctx.phase == Phase::Green)
            {
                ctx.phase = Phase::Porch2;
            }
            else if (ctx.phase == Phase::Blue)
            {
                ctx.phase = Phase::Sync;
            }
            else if (ctx.phase == Phase::Red)
            {
                render_line(ctx.line_index);
                SSTV_LOG_V("[SSTV] line %d done\n", ctx.line_index);
                ctx.line_index++;
                clear_accum();
                if (ctx.line_index >= ctx.line_count)
                {
                    set_status(sstv::State::Complete, static_cast<uint16_t>(ctx.line_count), 1.0f,
                               ctx.audio_level, true);
                    s_pending_save = true;
                    ctx.phase = Phase::Idle;
                    ctx.header_ok = false;
                    reset_header_state(ctx);
                    SSTV_LOG("[SSTV] complete\n");
                }
                else
                {
                    set_status(sstv::State::Receiving, static_cast<uint16_t>(ctx.line_index),
                               static_cast<float>(ctx.line_index) / ctx.line_count,
                               ctx.audio_level, true);
                    ctx.phase = Phase::Porch1;
                    ctx.phase_samples = 0;
                    reset_pixel_state(ctx);
                }
            }
        }
    }
}
} // namespace

namespace sstv
{

bool start()
{
    if (s_active)
    {
        return true;
    }

    set_error(nullptr);

    if (!s_frame)
    {
        s_frame = static_cast<uint16_t*>(malloc(kOutWidth * kOutHeight * sizeof(uint16_t)));
        if (!s_frame)
        {
            set_error("No framebuffer");
            set_status(State::Error, 0, 0.0f, 0.0f, false);
            return false;
        }
    }
    clear_frame();
    clear_saved_path();
    s_pending_save = false;

    s_stop = false;
    s_active = true;
    set_status(State::Waiting, 0, 0.0f, 0.0f, true);

    BaseType_t ok = xTaskCreate(
        sstv_task,
        "sstv_rx",
        kTaskStack,
        nullptr,
        6,
        &s_task);
    if (ok != pdPASS)
    {
        s_task = nullptr;
        s_active = false;
        set_error("Task create failed");
        set_status(State::Error, 0, 0.0f, 0.0f, false);
        return false;
    }

    return true;
}

void stop()
{
    if (!s_active)
    {
        return;
    }
    s_stop = true;
    for (int i = 0; i < 20; ++i)
    {
        if (!s_task)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_active = false;
}

bool is_active()
{
    return s_active;
}

Status get_status()
{
    Status out;
    portENTER_CRITICAL(&s_lock);
    out = s_status;
    portEXIT_CRITICAL(&s_lock);
    return out;
}

const char* get_last_error()
{
    return s_last_error;
}

const char* get_last_saved_path()
{
    return get_saved_path();
}

const char* get_mode_name()
{
    return vis_mode_name(s_vis_mode);
}

const uint16_t* get_framebuffer()
{
    return s_frame;
}

uint16_t frame_width()
{
    return kOutWidth;
}

uint16_t frame_height()
{
    return kOutHeight;
}

} // namespace sstv

#else

namespace sstv
{

bool start()
{
    return false;
}

void stop()
{
}

bool is_active()
{
    return false;
}

Status get_status()
{
    return {};
}

const char* get_last_error()
{
    return "SSTV not supported";
}

const char* get_last_saved_path()
{
    return "";
}

const uint16_t* get_framebuffer()
{
    return nullptr;
}

uint16_t frame_width()
{
    return 0;
}

uint16_t frame_height()
{
    return 0;
}

} // namespace sstv

#endif
